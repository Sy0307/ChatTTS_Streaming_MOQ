from flask import Flask, Response, stream_with_context, request
import time
import argparse
import struct
import sys, os  # 导入 sys 和 os
import numpy as np # 确保导入 numpy

# --- ChatTTS Setup ---
if sys.platform == "darwin":
    os.environ["PYTORCH_ENABLE_MPS_FALLBACK"] = "1"
now_dir = os.getcwd()
sys.path.append(now_dir)
import ChatTTS  # 导入 ChatTTS 模块
# 尝试直接从 ChatTTS 模块导入 float_to_int16，如果找不到，则使用下面的自定义函数
try:
    from tools.audio import float_to_int16
except ImportError:
    print("Warning: tools.audio.float_to_int16 not found in ChatTTS. Using fallback implementation.")
    def float_to_int16(audio_float):
        """
        Fallback implementation of float_to_int16 if not found in tools.audio.py.
        将 float 音频数据转换为 int16 PCM 数据.
        假设输入 audio_float 的范围是 [-1.0, 1.0].
        """
        audio_int16 = (audio_float * 32767).astype(np.int16) # 缩放到 int16 范围
        return audio_int16


# --- 全局 ChatTTS 模型 (应用启动时加载一次) ---
chat = None

app = Flask(__name__)

def load_chattts_model():
    global chat
    print("开始加载 ChatTTS 模型...")
    chat = ChatTTS.Chat()
    print("ChatTTS.Chat() 实例创建完成...")
    chat.load(compile=False)
    print("chat.load() 加载模型权重完成...")
    print("ChatTTS 模型加载完成！")

# --- Modified generate_audio_stream function (Simplified Header, Debugging) ---
def generate_audio_stream(text):
    """
    使用 ChatTTS 模型生成流式音频数据 (Simplified Header for debugging).
    发送 **初始 WAV header with placeholder sizes ONLY**.
    Header update is REMOVED for now to isolate header creation issues.
    """
    global chat

    if chat is None:
        print("ChatTTS 模型未加载. 请检查服务器启动日志。")
        yield b''
        return

    params_infer_code = ChatTTS.Chat.InferCodeParams(
        spk_emb=chat.sample_random_speaker(),
        temperature=0.3,
        top_P=0.7,
        top_K=20
    )

    try:
        streamchat = chat.infer(
            [text],
            skip_refine_text=True,
            stream=True,
            params_infer_code=params_infer_code
        )

        # --- *** 务必再次核对 ChatTTS 输出的音频格式 (采样率, 位深度, 声道数) *** ---
        sample_rate = 24000 # 假设 ChatTTS 输出 24kHz 音频 (请仔细核对!)
        bits_per_sample = 16 # 假设 ChatTTS 输出 16-bit 音频 (请仔细核对!)
        channels = 1 # 假设 ChatTTS 输出 Mono 音频 (请仔细核对!)
        byte_rate = sample_rate * channels * bits_per_sample // 8
        block_align = channels * bits_per_sample // 8

        # --- *** Re-verified struct.pack format string and arguments for WAV header *** ---
        header = struct.pack('<4sI4s4sIHHIIHH4sI',
                             b'RIFF',
                             36,  # ChunkSize (PLACEHOLDER - using initial 36 for now)
                             b'WAVE',
                             b'fmt ',
                             16,
                             1,    # AudioFormat (PCM)
                             channels,
                             sample_rate,
                             byte_rate,
                             block_align,
                             bits_per_sample,
                             b'data',
                             0)   # DataSubchunkSize (PLACEHOLDER - using initial 0 for now)


        header_list = [header]
        total_data_size = 0

        print(f"Initial WAV Header (bytes): {header_list[0].hex()}") # 打印初始 WAV 头部 (hex 形式)
        yield header_list[0] # Send initial header with placeholder sizes


        for stream_wav in streamchat: # 直接迭代 streamchat (ChatTTS 的流式输出)
            if len(stream_wav.shape) == 3:
                stream_wav = stream_wav[0] # 假设输出 shape 是 [1, C, T], 取第一个 batch

            # --- 直接在 generate_audio_stream 中进行格式化 ---
            pcm16_byte_data = float_to_int16(stream_wav) # 转换为 PCM16
            audio_chunk_bytes = pcm16_byte_data.astype("<i2").tobytes() # 转换为 bytes

            chunk_len = len(audio_chunk_bytes) # 获取当前 chunk 的大小
            total_data_size += chunk_len # 累加音频数据大小

            print(f"Yielding ChatTTS audio chunk, shape: {stream_wav.shape}, dtype: {stream_wav.dtype}, abs_max: {np.abs(stream_wav).max()}, chunk_bytes: {chunk_len} bytes, total_data_size: {total_data_size}") # 打印更详细的音频数据信息
            # print(f"Audio chunk bytes (first 20 bytes): {audio_chunk_bytes[:20].hex()}") # 可选: 打印音频数据字节 (hex 形式，只打印前 20 字节)
            yield audio_chunk_bytes


        # --- WAV Header Update REMOVED for now (for debugging) ---
        # chunk_size_final = data_chunk_size_final + 36
        # updated_header = struct.pack('<4sI4s4sIHHIIHH4sI',
        #                      b'RIFF',
        #                      chunk_size_final,
        #                      # ... (rest of header update code commented out) ... )
        # header_bytes = updated_header
        # header_list[0] = header_bytes

        # print(f"Updated WAV Header (bytes): {updated_header.hex()}, data_chunk_size_final: {data_chunk_size_final}, chunk_size_final: {chunk_size_final}") # No updated header printing


    except Exception as e:
        print(f"ChatTTS 语音合成出错: {e}")
        yield b''


@app.route('/stream_audio')
def stream_audio_endpoint():
    """
    Flask 流式音频 API 接口 (ChatTTS 集成版, 移除 ChatStreamer, 添加详细调试信息, 修正 ChunkSize 计算).
    """
    text = request.args.get('text', '你好，请问有什么可以帮您?')
    return Response(stream_with_context(generate_audio_stream(text)), mimetype='audio/wav')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Flask Streaming Audio API Launch with ChatTTS")
    parser.add_argument("--host", type=str, default="0.0.0.0", help="API server host")
    parser.add_argument("--port", type=int, default=8000, help="API server port")
    args = parser.parse_args()

    load_chattts_model() # 应用启动时加载 ChatTTS 模型

    app.run(host=args.host, port=args.port, debug=False)