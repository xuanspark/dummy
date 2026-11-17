// ============================================================================
//  ascii_processor.cpp —— ASCII 行级协议解析器
// ----------------------------------------------------------------------------
//  - 作为原生 Fibre 协议之外的人类可读调试入口
//  - 按换行符切分命令，并交给上层 ASCII 处理函数执行
//  - 仅处理解析与分发，不直接承载机器人业务状态机
// ============================================================================

/*
* The ASCII protocol is a simpler, human readable alternative to the main native
* protocol.
* In the future this protocol might be extended to support selected GCode commands.
* For a list of supported commands see doc/ascii-protocol.md
*/

/* Includes ------------------------------------------------------------------*/

#include "common_inc.h"
#include "ascii_processor.hpp"

/* Private macros ------------------------------------------------------------*/
/* Private typedef -----------------------------------------------------------*/
/* Global constant data ------------------------------------------------------*/
/* Global variables ----------------------------------------------------------*/
/* Private constant data -----------------------------------------------------*/
#define MAX_LINE_LENGTH 256
#define TO_STR_INNER(s) #s
#define TO_STR(s) TO_STR_INNER(s)

bool EmergencyStopScanner::Feed(const uint8_t* data, size_t length)
{
    bool stopped = false;
    for (size_t i = 0; i < length; ++i) {
        char c = static_cast<char>(data[i]);
        if (c == 0) overflow = true;
        if (c == '\r' || c == '\n') {
            while (used && (line[used - 1] == ' ' || line[used - 1] == '\t')) --used;
            line[used] = 0;
            const char* command = line;
            bool valid = !overflow;
            if (line[0] == 'T' && line[1] == ':') {
                const char* number = line + 2;
                uint32_t request = 0;
                int digits = 0;
                while (*number >= '0' && *number <= '9' && digits < 10) {
                    uint32_t digit = *number++ - '0';
                    if (request > (2147483647u - digit) / 10) valid = false;
                    request = request * 10 + digit;
                    ++digits;
                }
                valid = valid && digits && request && *number == ' ';
                command = *number == ' ' ? number + 1 : number;
            }
            if (valid && !strcmp(command, "!STOP")) stopped = true;
            used = 0;
            overflow = false;
        } else if (used < sizeof(line) - 1) {
            line[used++] = c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
        } else overflow = true;
    }
    return stopped;
}
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Function implementations --------------------------------------------------*/


// @brief Executes an ASCII protocol command
// @param buffer buffer of ASCII encoded characters
// @param len size of the buffer
void ASCII_protocol_process_line(const uint8_t* buffer, size_t len, StreamSink &response_channel)
{
    static_assert(sizeof(char) == sizeof(uint8_t));

    // copy everything into a local buffer so we can insert null-termination
    char cmd[MAX_LINE_LENGTH + 1];
    if (len > MAX_LINE_LENGTH) len = MAX_LINE_LENGTH;
    memcpy(cmd, buffer, len);

    cmd[len] = 0; // null-terminate

    if (response_channel.channelType == StreamSink::CHANNEL_TYPE_USB)
        OnUsbAsciiCmd(cmd, len, response_channel);
    else if (response_channel.channelType == StreamSink::CHANNEL_TYPE_UART4)
        OnUart4AsciiCmd(cmd, len, response_channel);
    else if (response_channel.channelType == StreamSink::CHANNEL_TYPE_UART5)
        OnUart5AsciiCmd(cmd, len, response_channel);
}

void ASCII_protocol_parse_stream(const uint8_t* buffer, size_t len, StreamSink &response_channel)
{
    // 每个物理通道独立组行，避免 USB 与 UART 的分包相互拼接。
    struct ParserState {
        uint8_t buffer[MAX_LINE_LENGTH];
        bool active = true;
        uint32_t index = 0;
    };
    static ParserState parsers[3];
    unsigned index = response_channel.channelType == StreamSink::CHANNEL_TYPE_USB ? 0 :
        response_channel.channelType == StreamSink::CHANNEL_TYPE_UART4 ? 1 : 2;
    auto& parser = parsers[index];
    auto& parse_buffer = parser.buffer;
    auto& read_active = parser.active;
    auto& parse_buffer_idx = parser.index;

    while (len--)
    {
        // if the line becomes too long, reset buffer and wait for the next line
        if (parse_buffer_idx >= MAX_LINE_LENGTH)
        {
            read_active = false;
            parse_buffer_idx = 0;
        }

        // Fetch the next char
        uint8_t c = *(buffer++);
        bool is_end_of_line = (c == '\r' || c == '\n');
        if (is_end_of_line)
        {
            if (read_active && parse_buffer_idx != 0)
                ASCII_protocol_process_line(parse_buffer, parse_buffer_idx, response_channel);
            parse_buffer_idx = 0;
            read_active = true;
        } else
        {
            if (read_active)
            {
                parse_buffer[parse_buffer_idx++] = c;
            }
        }
    }
}
