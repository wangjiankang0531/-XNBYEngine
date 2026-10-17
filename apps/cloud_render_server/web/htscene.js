// htscene.js — 鸿图云渲染 JS SDK *传输层*(WebRTC 连接 + 命令门面)
//
// 在 htscene-protocol.js(纯协议)之上加完整 WebRTC 握手。命令门面本身全部来自
// 协议层 createScene —— 本文件只负责"连上 + 把命令送过去"。
//
// 用法(完整握手):
//   const scene = await HtScene.connect({ video, onStatus });
//   scene.polyline.add({ mode:'local', vertices:[[0,0,0],[10,5,0]], style:{...} });
//   scene.entity.hideModel(101);
//   scene.close();
//
// 用法(自管 transport,只要命令门面):
//   const api = HtScene.attach(obj => myChannel.send(JSON.stringify(obj)));
//
// 需先加载 htscene-protocol.js(<script> 顺序在前,或 CommonJS/AMD 自动依赖)。
// 协议与字段含义见 docs/manual/21-cloud-render-sdk.md。
(function (root, factory) {
  if (typeof module === 'object' && module.exports)
    module.exports = factory(require('./htscene-protocol.js'));
  else if (typeof define === 'function' && define.amd)
    define(['./htscene-protocol'], factory);
  else
    root.HtScene = factory(root.HtSceneProtocol);
})(typeof self !== 'undefined' ? self : this, function (Protocol) {
  'use strict';

  if (!Protocol || typeof Protocol.createScene !== 'function')
    throw new Error('htscene.js 需要先加载 htscene-protocol.js');

  // attach:外部自管 transport(传 send(obj) 函数)→ 协议门面。
  function attach(send) { return Protocol.createScene(send); }

  // connect:完整 WebRTC 握手(服务端是 offer 方),返回带命令 API 的 scene。
  // opts: { video?, baseUrl='', onStatus?(state), onTrack?(ev), onSend?(cmd),
  //         control=true, rtcConfig?, fetch? }
  // onStatus 状态码:'connecting'|'negotiated'|'live'|'lost'|'closed'|
  //                  'signaling-failed'
  // onSend:每条出站命令(对象)发送前的回调(调试 / 命令日志 / 调试台用)。
  async function connect(opts) {
    opts = opts || {};
    var baseUrl  = opts.baseUrl || '';
    var video    = opts.video || null;
    var onStatus = opts.onStatus || function () {};
    var fetchImpl = opts.fetch || fetch;
    var dc = null;

    onStatus('connecting');
    var send = function (obj) {
      if (opts.onSend) { try { opts.onSend(obj); } catch (e) {} }
      if (dc && dc.readyState === 'open') dc.send(JSON.stringify(obj));
    };
    var api = Protocol.createScene(send);

    var sess = await fetchImpl(baseUrl + '/session', {
      method: 'POST',
      body: JSON.stringify({ control: opts.control !== false }),
    });
    if (!sess.ok) { onStatus('signaling-failed'); throw new Error('session ' + sess.status); }
    var offer = await sess.json();

    var pc = new RTCPeerConnection(opts.rtcConfig || {});
    pc.ontrack = function (ev) {
      if (video) video.srcObject = ev.streams[0] || new MediaStream([ev.track]);
      if (opts.onTrack) opts.onTrack(ev);
    };
    pc.ondatachannel = function (ev) {
      dc = ev.channel;
      dc.onopen  = function () { onStatus('live'); };
      dc.onclose = function () { onStatus('closed'); };
    };
    pc.onconnectionstatechange = function () {
      if (pc.connectionState === 'failed' || pc.connectionState === 'disconnected')
        onStatus('lost');
    };

    await pc.setRemoteDescription({ type: 'offer', sdp: offer.sdp });
    await pc.setLocalDescription(await pc.createAnswer());
    // 非 trickle:等 ICE 候选收集齐再回 answer。
    await new Promise(function (res) {
      if (pc.iceGatheringState === 'complete') return res();
      pc.onicegatheringstatechange = function () {
        if (pc.iceGatheringState === 'complete') res();
      };
    });

    var resp = await fetchImpl(baseUrl + '/answer', {
      method: 'POST',
      body: JSON.stringify({ id: offer.id, sdp: pc.localDescription.sdp }),
    });
    if (!resp.ok) { onStatus('signaling-failed'); throw new Error('answer ' + resp.status); }
    onStatus('negotiated');

    api.pc = pc;
    api.close = function () { try { pc.close(); } catch (e) {} };
    return api;
  }

  return {
    connect: connect,
    attach: attach,
    create: attach,
    protocol: Protocol,
    version: '1.0.0',
  };
});
