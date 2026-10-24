#ifndef __ASCII_PROTOCOL_H
#define __ASCII_PROTOCOL_H


/* ----- 包含头文件 ------------------------------------------------ */
#include <fibre/protocol.hpp>

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

/* ----- 导出类型 / 常量 / 变量 / 宏 / 函数 ----------------- */

/* ----- 导出函数 ------------------------------------------------ */
void ASCII_protocol_parse_stream(const uint8_t* buffer, size_t len, StreamSink& response_channel);
void OnUsbAsciiCmd(const char* _cmd, size_t _len, StreamSink& _responseChannel);
void OnUart4AsciiCmd(const char* _cmd, size_t _len, StreamSink& _responseChannel);
void OnUart5AsciiCmd(const char* _cmd, size_t _len, StreamSink& _responseChannel);

// 向指定通道（UART 或 USB-VCP）单向发送响应。
// 请使用本函数而不是 printf：printf 会同时复制到所有通道。
//
// 注意：这里把 "<text>\r\n" 合并到 *同一次* process_bytes 调用。原版本拆成两次，
// USB CDC 路径下每次 process_bytes 都要单独抢 sem_usb_tx（仅 10ms 超时），
// 当后一次抢不到时返回 -1，"\r\n" 直接被丢；下一次 Respond 第一个分包又会
// 与上一次未完成的 IN 抢，于是出现"只发出第一行，后续 N 行全丢"的现象。
// 一次发完整行后，每个 Respond 只占 1 个 USB packet，不再有这个竞态。
// 输出字节流（"<text>\r\n"）保持完全一致。1
template<typename ... TArgs>
void Respond(StreamSink &output , const char *fmt, TArgs &&... args)
{
    char response[66];  // 64 + "\r\n"
    size_t len = snprintf(response, sizeof(response) - 2, fmt, std::forward<TArgs>(args)...);
    if (len > sizeof(response) - 2) len = sizeof(response) - 2;  // snprintf 返回值可能 > size
    response[len++] = '\r';
    response[len++] = '\n';
    output.process_bytes((uint8_t *) response, len, nullptr);
}


#endif /* __ASCII_PROTOCOL_H */
