# ChatTTS_Streaming_MOQ

![image.png](https://s2.loli.net/2025/02/19/CrdGOmoqtSJMBv5.png)

本项目是为原项目[ChatTTS](https://github.com/2noise/ChatTTS)的流式传输扩展，并且作为MOQ的实际应用而生。
关于MOQ, 请参考[MOQ](https://datatracker.ietf.org/doc/draft-ietf-moq-transport/)
本项目尚不完善，有任何问题可通过`Issues`反馈。

- [x] 实现ChatTTS的流式传输的封装
- [x] 实现proxy代理
- [ ] 实现MOQ的server
- [ ] 实现MOQ的client

## Quick Start

```bash
# 安装原项目的依赖 详见https://github.com/2noise/ChatTTS?tab=readme-ov-file#get-started

make # 保证 make 正常运行的依赖

python api_streaming_flask.py --host 0.0.0.0 --port 8000 # 启动原ChatTTS的server

./c_proxy_server # 启动proxy代理

```

如若没有报错，那你现在已经完成部署。

可以发送任意命令来测试，例如:

```bash
curl "http://0.0.0.0:8000/stream_audio?text=%E4%BD%A0%E5%A5%BD%EF%BC%8C%E8%BF%99%E6%98%AF%E4%B8%80%E4%B8%AA%20Flask%20%E6%B5%81%E5%BC%8F%E9%9F%B3%E9%A2%91%20API%20%E6%B5%8B%E8%AF%95%E3%80%82" -o output_test.wav   
```

再使用VLC等播放器播放`output_test.wav`文件。（推荐使用VLC播放，其余播放器可能会出现问题，待解决）

## 感谢

感谢的原项目的作者们[ChatTTS](https://github.com/2noise/ChatTTS)
