// ============================================================================
//  cli.c —— 串口控制台命令行实现
// ----------------------------------------------------------------------------
//  - 负责处理从串口进入的单字节输入，BS/CR/LF 、行缓冲、命令分发
//  - 支持 wifi (add/del/list/connect/快捷)、ls/cat/rm、ll、reboot、info 等
//  - 多 SSID 以 JSON 数组存 WIFI_FILE，最多 WIFI_MAX_APS 个
//  - 帮助文本中的中文采用 \xE? UTF-8 转义字节，使源文件保持纯 ASCII
//    以避免某些锁定编诛器编译上的多字节问题。请勿随意增改该部分。
// ============================================================================
#include "main.h"

#define WIFI_MAX_APS 5  // 同时保存的最多 SSID 数

// wifi_parse：解析 wifi.json，返回读出的 SSID 个数。
//   - 支持 [{ssid,pass}, ...] 数组格式 与 单个 {ssid,pass} 历史格式
static int wifi_parse(const char *json, char ss[][33], char pp[][65]) {
  int n = 0;
  if (json == NULL) return 0;
  if (json[0] == '[') {
    for (int i = 0; i < WIFI_MAX_APS; i++) {
      char path[20];
      snprintf(path, sizeof(path), "$[%d].ssid", i);
      char *s = mg_json_get_str(mg_str(json), path);
      if (s == NULL) break;
      snprintf(path, sizeof(path), "$[%d].pass", i);
      char *p = mg_json_get_str(mg_str(json), path);
      strncpy(ss[n], s, 32); ss[n][32] = '\0';
      strncpy(pp[n], p ? p : "", 64); pp[n][64] = '\0';
      n++;
      free(s);
      if (p) free(p);
    }
  } else if (json[0] == '{') {
    char *s = mg_json_get_str(mg_str(json), "$.ssid");
    char *p = mg_json_get_str(mg_str(json), "$.pass");
    if (s) {
      strncpy(ss[0], s, 32); ss[0][32] = '\0';
      strncpy(pp[0], p ? p : "", 64); pp[0][64] = '\0';
      n = 1;
      free(s);
    }
    if (p) free(p);
  }
  return n;
}

// wifi_save：将 SSID 数组以 JSON 写回 WIFI_FILE（覆盖式写入）。
static void wifi_save(char ss[][33], char pp[][65], int n) {
  FILE *fp = fopen(WIFI_FILE, "w");
  if (fp) {
    fprintf(fp, "[");
    for (int i = 0; i < n; i++) {
      if (i > 0) fprintf(fp, ",");
      fprintf(fp, "{\"ssid\":\"%s\",\"pass\":\"%s\"}", ss[i], pp[i]);
    }
    fprintf(fp, "]");
    fclose(fp);
  }
}

// cli_wifi_add：添加或更新一个 SSID 凭证。列表满时拒绝。
static void cli_wifi_add(const char *ssid, const char *pass) {
  char *json = mg_file_read(&mg_fs_posix, WIFI_FILE, NULL);
  char ss[WIFI_MAX_APS][33], pp[WIFI_MAX_APS][65];
  int n = wifi_parse(json, ss, pp);
  free(json);
  for (int i = 0; i < n; i++) {
    if (strcmp(ss[i], ssid) == 0) {
      strncpy(pp[i], pass, 64); pp[i][64] = '\0';
      wifi_save(ss, pp, n);
      printf("--- WiFi '%s' updated\n", ssid);
      return;
    }
  }
  if (n >= WIFI_MAX_APS) {
    printf("--- WiFi list full (max %d)\n", WIFI_MAX_APS);
    return;
  }
  strncpy(ss[n], ssid, 32); ss[n][32] = '\0';
  strncpy(pp[n], pass, 64); pp[n][64] = '\0';
  wifi_save(ss, pp, n + 1);
  printf("--- WiFi '%s' added (%d/%d)\n", ssid, n + 1, WIFI_MAX_APS);
}

// cli_wifi_del：按 SSID 删除一条凭证。
static void cli_wifi_del(const char *ssid) {
  char *json = mg_file_read(&mg_fs_posix, WIFI_FILE, NULL);
  char ss[WIFI_MAX_APS][33], pp[WIFI_MAX_APS][65];
  int n = wifi_parse(json, ss, pp);
  free(json);
  int f = -1;
  for (int i = 0; i < n; i++) {
    if (strcmp(ss[i], ssid) == 0) { f = i; break; }
  }
  if (f < 0) { printf("--- WiFi '%s' not found\n", ssid); return; }
  for (int i = f; i < n - 1; i++) {
    memcpy(ss[i], ss[i + 1], 33);
    memcpy(pp[i], pp[i + 1], 65);
  }
  wifi_save(ss, pp, n - 1);
  printf("--- WiFi '%s' deleted\n", ssid);
}

// cli_wifi_list：列出已保存的 SSID（不包含密码）。
static void cli_wifi_list(void) {
  char *json = mg_file_read(&mg_fs_posix, WIFI_FILE, NULL);
  char ss[WIFI_MAX_APS][33], pp[WIFI_MAX_APS][65];
  int n = wifi_parse(json, ss, pp);
  free(json);
  if (n == 0) { printf("--- No WiFi saved\n"); return; }
  printf("--- Saved WiFi (%d):\n", n);
  for (int i = 0; i < n; i++) printf("---   %d. %s\n", i + 1, ss[i]);
}

// cli_wifi_connect：逐个尝试已保存的 SSID，首个联上后重启以走 app_main 的启动流程。
static void cli_wifi_connect(void) {
  char *json = mg_file_read(&mg_fs_posix, WIFI_FILE, NULL);
  char ss[WIFI_MAX_APS][33], pp[WIFI_MAX_APS][65];
  int n = wifi_parse(json, ss, pp);
  free(json);
  if (n == 0) { printf("--- No WiFi saved. Use: wifi add SSID PASS\n"); return; }
  for (int i = 0; i < n; i++) {
    printf("--- Trying %d/%d: %s ...\n", i + 1, n, ss[i]);
    if (wifi_init(ss[i], pp[i])) {
      printf("--- Connected! Rebooting...\n");
      esp_restart();
      return;
    }
    printf("--- Failed: %s\n", ss[i]);
  }
  printf("--- All WiFi failed\n");
}

static void cli_ls(void) {
  DIR *dirp = opendir(FS_ROOT);
  struct dirent *dp;
  if (dirp == NULL) {
    MG_ERROR(("Cannot open FS: %d", errno));
  } else {
    while ((dp = readdir(dirp)) != NULL) {
      /* Do not show current and parent dirs */
      if (strcmp((const char *) dp->d_name, ".") == 0 ||
          strcmp((const char *) dp->d_name, "..") == 0) {
        continue;
      } else {
        printf("%s\n", dp->d_name);
      }
    }
    closedir(dirp);
  }
}

static void cli_cat(const char *fname) {
  char path[MG_PATH_MAX];
  snprintf(path, sizeof(path), "%s/%s", FS_ROOT, fname);
  FILE *fp = fopen(path, "r");
  if (fp != NULL) {
    int ch;
    while ((ch = fgetc(fp)) != EOF) putchar(ch);
    fclose(fp);
  }
}

static void cli_rm(const char *fname) {
  char path[100];
  snprintf(path, sizeof(path), "%s/%s", FS_ROOT, fname);
  remove(path);
}

static struct mg_iobuf in;

// cli：处理单个输入字节，收到 \n 后触发命令解析。非阻塞（0/0xFF 直接返回）。
void cli(uint8_t input_byte) {
  if (input_byte == 0 || input_byte == 0xff) return;

  // 处理退格（BS=0x08, DEL=0x7F）
  if (input_byte == 0x08 || input_byte == 0x7f) {
    if (in.len > 0) {
      in.len--;
      printf("\b \b");  // erase character on terminal
      fflush(stdout);
    }
    return;
  }

  // 本地回显：CR/LF 统一输出为 \r\n，其余字符原样输出
  if (input_byte == '\r' || input_byte == '\n') {
    printf("\r\n");
  } else {
    putchar(input_byte);
  }
  fflush(stdout);

  if (in.len >= 128) in.len = 0;                // 超长行丢弃（防缓冲区越界）
  if (input_byte == '\r') input_byte = '\n';   // 以 \n 作为唯一行结束符
  mg_iobuf_add(&in, in.len, &input_byte, sizeof(input_byte));

  if (input_byte == '\n') {
    const char *arrow = "---";
    char buf0[10], buf1[50], buf2[250];

    in.buf[in.len] = '\0';
    buf0[0] = buf1[0] = buf2[0] = '\0';
    sscanf((char *) in.buf, "%9s %49s %249[^\r\n]", buf0, buf1, buf2);

    // help / ? / 空输入：输出帮助文本，不带错误前缀
    if (buf0[0] == '\0' || strcmp(buf0, "help") == 0 ||
        strcmp(buf0, "?") == 0 || strcmp(buf0, "h") == 0) {
      printf("%s Available commands / \xE5\x91\xBD\xE4\xBB\xA4\xE5\x88\x97\xE8\xA1\xA8:\n", arrow);
      printf("%s \n", arrow);
      printf("%s [General / \xE9\x80\x9A\xE7\x94\xA8]\n", arrow);
      printf("%s   help | ? | h\n", arrow);
      printf("%s       \xE6\x98\xBE\xE7\xA4\xBA\xE6\x9C\xAC\xE5\xB8\xAE\xE5\x8A\xA9\xE4\xBF\xA1\xE6\x81\xAF (show this help)\n", arrow);
      printf("%s   reboot\n", arrow);
      printf("%s       \xE9\x87\x8D\xE5\x90\xAF ESP32 \xE8\xAE\xBE\xE5\xA4\x87\xEF\xBC\x8C\xE5\xB8\xB8\xE7\x94\xA8\xE4\xBA\x8E\xE5\x88\x87\xE6\x8D\xA2 WiFi \xE5\x90\x8E\xE7\x94\x9F\xE6\x95\x88\n", arrow);
      printf("%s   ll LEVEL\n", arrow);
      printf("%s       \xE8\xAE\xBE\xE7\xBD\xAE\xE6\x97\xA5\xE5\xBF\x97\xE7\xAD\x89\xE7\xBA\xA7 (0=\xE5\x85\xB3\xE9\x97\xAD, 1=Error, 2=Info, 3=Debug, 4=Verbose)\n", arrow);
      printf("%s       \xE4\xBE\x8B: ll 2\n", arrow);
      printf("%s \n", arrow);
      printf("%s [WiFi / \xE6\x97\xA0\xE7\xBA\xBF\xE7\xBD\x91\xE7\xBB\x9C]\n", arrow);
      printf("%s   wifi list\n", arrow);
      printf("%s       \xE5\x88\x97\xE5\x87\xBA\xE5\xB7\xB2\xE4\xBF\x9D\xE5\xAD\x98\xE7\x9A\x84 WiFi \xE9\x85\x8D\xE7\xBD\xAE (list saved SSIDs)\n", arrow);
      printf("%s   wifi add SSID [PASS]\n", arrow);
      printf("%s       \xE6\xB7\xBB\xE5\x8A\xA0\xE4\xB8\x80\xE4\xB8\xAA WiFi \xEF\xBC\x88\xE4\xBF\x9D\xE5\xAD\x98\xE5\x88\xB0 NVS\xEF\xBC\x89\n", arrow);
      printf("%s       \xE4\xBE\x8B: wifi add MyHome 12345678\n", arrow);
      printf("%s   wifi del SSID\n", arrow);
      printf("%s       \xE5\x88\xA0\xE9\x99\xA4\xE6\x8C\x87\xE5\xAE\x9A\xE7\x9A\x84 WiFi \xE9\x85\x8D\xE7\xBD\xAE\n", arrow);
      printf("%s   wifi connect\n", arrow);
      printf("%s       \xE4\xBE\x9D\xE6\xAC\xA1\xE5\xB0\x9D\xE8\xAF\x95\xE8\xBF\x9E\xE6\x8E\xA5\xE6\x89\x80\xE6\x9C\x89\xE5\xB7\xB2\xE4\xBF\x9D\xE5\xAD\x98\xE7\x9A\x84 WiFi\n", arrow);
      printf("%s   wifi SSID [PASS]\n", arrow);
      printf("%s       \xE5\xBF\xAB\xE6\x8D\xB7\xE6\x96\xB9\xE5\xBC\x8F\xEF\xBC\x9A\xE5\x90\x8C\xE6\x97\xB6 add + \xE7\xAB\x8B\xE5\x8D\xB3\xE8\xBF\x9E\xE6\x8E\xA5\xEF\xBC\x8C\xE6\x88\x90\xE5\x8A\x9F\xE5\x90\x8E\xE8\x87\xAA\xE5\x8A\xA8\xE9\x87\x8D\xE5\x90\xAF\n", arrow);
      printf("%s       \xE4\xBE\x8B: wifi MyHome 12345678\n", arrow);
      printf("%s \n", arrow);
      printf("%s [Filesystem / \xE6\x96\x87\xE4\xBB\xB6\xE7\xB3\xBB\xE7\xBB\x9F]\n", arrow);
      printf("%s   ls\n", arrow);
      printf("%s       \xE5\x88\x97\xE5\x87\xBA\xE5\x86\x85\xE9\x83\xA8\xE5\xAD\x98\xE5\x82\xA8\xE7\x9A\x84\xE6\x89\x80\xE6\x9C\x89\xE6\x96\x87\xE4\xBB\xB6\xE5\x92\x8C\xE5\xA4\xA7\xE5\xB0\x8F\n", arrow);
      printf("%s   cat FILE\n", arrow);
      printf("%s       \xE8\xBE\x93\xE5\x87\xBA\xE6\x96\x87\xE4\xBB\xB6\xE5\x86\x85\xE5\xAE\xB9 (print file to stdout)\n", arrow);
      printf("%s       \xE4\xBE\x8B: cat config.json\n", arrow);
      printf("%s   rm FILE\n", arrow);
      printf("%s       \xE5\x88\xA0\xE9\x99\xA4\xE6\x96\x87\xE4\xBB\xB6 (remove file)\n", arrow);
      printf("%s \n", arrow);
      printf("%s CLI output end\n", arrow);
      in.len = 0;
      return;
    }

    printf("%s CLI command: '%s'\n", arrow, buf0);
    if (strcmp(buf0, "reboot") == 0) {
      esp_restart();
    } else if (strcmp(buf0, "info") == 0 || strcmp(buf0, "banner") == 0) {
      print_banner();
    } else if (strcmp(buf0, "ls") == 0) {
      cli_ls();
    } else if (strcmp(buf0, "cat") == 0) {
      cli_cat(buf1);
    } else if (strcmp(buf0, "rm") == 0) {
      cli_rm(buf1);
    } else if (strcmp(buf0, "ll") == 0) {
      mg_log_set(atoi(buf1));
    } else if (strcmp(buf0, "wifi") == 0) {
      if (strcmp(buf1, "list") == 0) {
        cli_wifi_list();
      } else if (strcmp(buf1, "del") == 0) {
        if (buf2[0] == '\0') {
          printf("%s Usage: wifi del SSID\n", arrow);
        } else {
          cli_wifi_del(buf2);
        }
      } else if (strcmp(buf1, "add") == 0) {
        char ws[50], wp[250];
        ws[0] = wp[0] = '\0';
        sscanf(buf2, "%49s %249[^\r\n]", ws, wp);
        if (ws[0] == '\0') {
          printf("%s Usage: wifi add SSID [PASS]\n", arrow);
        } else {
          cli_wifi_add(ws, wp);
        }
      } else if (strcmp(buf1, "connect") == 0) {
        cli_wifi_connect();
      } else if (buf1[0] != '\0') {
        cli_wifi_add(buf1, buf2);
        printf("%s Connecting to %s ...\n", arrow, buf1);
        if (wifi_init(buf1, buf2)) {
          printf("%s Connected! Rebooting...\n", arrow);
          esp_restart();
        } else {
          printf("%s Failed. WiFi saved, try 'wifi connect' to retry all\n", arrow);
        }
      } else {
        printf("%s %s\n", arrow, "Usage: wifi add|del|list|connect|SSID");
      }
    } else {
      printf("%s %s\n", arrow, "Unknown command. Usage:");
      printf("%s %s\n", arrow, "  ll LEVEL (set log level)");
      printf("%s %s\n", arrow, "  rm FILENAME");
      printf("%s %s\n", arrow, "  cat FILENAME");
      printf("%s %s\n", arrow, "  ls");
      printf("%s %s\n", arrow, "  reboot");
      printf("%s %s\n", arrow, "  wifi add SSID [PASS]");
      printf("%s %s\n", arrow, "  wifi del SSID");
      printf("%s %s\n", arrow, "  wifi list");
      printf("%s %s\n", arrow, "  wifi connect");
      printf("%s %s\n", arrow, "  wifi SSID PASS (shortcut)");
    }
    printf("%s %s\n", arrow, "CLI output end");
    in.len = 0;
  }
}

void cli_init(void) {
  mg_iobuf_init(&in, 0, 32);
}
