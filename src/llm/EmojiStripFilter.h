#pragma once

// EmojiStripFilter — 流式响应文本中的 emoji 剥离过滤器（纯 C++，无 Qt 依赖）。
//
// 背景：模型回复（content / reasoning 增量）经常夹带 emoji，而衬线主题字体
// （Noto Serif SC）没有 emoji 字形：键帽 emoji（1️⃣）退化为"裸数字+残框"，
// 与暖墨单色主题冲突。本过滤器放在流式管道的咽喉处，让显示、持久化、
// 复制、后续对话上下文四层全部无 emoji。
//
// 设计要点：
// - 流安全：网络分块可能把一个多字节 UTF-8 字符从中间切开，内部缓存
//   不完整尾字节，跨 feed() 调用正确拼接后再处理，绝不产生半个字符。
// - 键帽 emoji 归一化为纯文本（1️⃣→"1."、🔟→"10."、#️⃣→"#"、*️⃣→"*"），
//   与 QML 显示层 normalizeKeycapEmoji 语义一致（那一层继续对历史消息兜底）。
// - 仅剥离 emoji 相关码位：U+1F000–U+1FAFF、U+2600–U+27BF、U+2B00–U+2BFF、
//   变体选择符 U+FE00–U+FE0F、键帽框 U+20E3、零宽连接符 U+200D。
//   普通中英文、标点（含 《》——→·）不受影响。
// - 工具调用参数与工具结果（如章节正文）不经此过滤器 —— 那是用户的创作
//   内容，可能合法包含 emoji 图形。
//
// 使用方式（流式）：
//   EmojiStripFilter f;                 // content 与 reasoning 各用一个实例
//   out += f.feed(chunk_delta);         // 每段增量调用
//   out += f.finish();                  // 流结束（finish_reason）时冲刷残余

#include <string>

namespace llm {

class EmojiStripFilter {
public:
    // 处理一段增量文本，返回过滤后的文本；内部状态跨调用保留。
    std::string feed(const std::string& chunk);

    // 流结束时冲刷残余状态：结尾孤立数字/未完成的键帽序列按"保留数字、
    // 丢弃装饰码位"处理；不完整的 UTF-8 尾字节按无效数据丢弃。
    std::string finish();

    // 单次处理完整文本（非流式路径），无状态、可并发调用。
    static std::string stripOnce(const std::string& text);

    // 复位到初始状态（StreamingPipeline 复用时应调用）。
    void reset();

private:
    // 解码得到一个码位（false 表示流已耗尽）。
    bool nextCp(char32_t& cp);

    std::string emitCp(char32_t cp) const;  // 码位 → UTF-8 输出串

    // 未完成的 UTF-8 尾字节（上次 feed 截断的部分）。
    std::string pending_;
    // 键帽检测：仅持有"数字 / # / *"单码位，等待下一个码位决定是键帽还是普通文本；
    // mark_seen 表示已收到 U+FE0F、正在等待键帽框 U+20E3。
    bool hold_active_ = false;
    bool hold_mark_seen_ = false;
    char32_t hold_cp_ = 0;
};

} // namespace llm
