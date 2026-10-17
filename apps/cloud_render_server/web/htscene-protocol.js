// htscene-protocol.js — 鸿图云渲染 *协议层*(纯命令构建,零依赖,UMD)
//
// 只负责把方法调用拼成 datachannel 的 {ns,op,id,element} 信封,交给调用方传入
// 的 send(obj) 发出去 —— 不含任何 WebRTC / 网络。传输无关:send 可以是
// datachannel.send 的包装、WebSocket、postMessage、或"收集进数组"(测试/批处理)。
//
//   const out = [];
//   const scene = createScene(c => out.push(c));   // “发送”=塞数组
//   scene.model.update(101, { pos:[1,2,0] });      // out = [{ns:'model',op:'update',...}]
//
// 这是 player.html 的 htscene.js 与演练 eventToScene.js 共用的**唯一协议定义**,
// 收敛信封 / entity 稳定 id 语义 / id 分段约定,避免两处手搓导致漂移。
//
// ⚠ 跨工程同步:本文件在 drill/drill-web/src/lib/htscene-protocol.js 有一份 ESM
//   副本(同核心)。改协议核心(createScene / 常量)时两处一起改。
//   协议与字段含义见 docs/manual/21-cloud-render-sdk.md。
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else if (typeof define === 'function' && define.amd) define([], factory);
  else root.HtSceneProtocol = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  // 编辑器稳定 id 在小号段(1..N,通常 N 远小于百万)。演练/前端动态新增的对象
  // 必须用这个高号段起的 id,才不会撞上"编辑器里预放实体"的 id。
  var DRILL_DYNAMIC_BASE = 10000000;

  // 单调动态 id 分配器(从 base 起)。每个会话一个,保证同会话内不重号。
  function makeDynamicIdAllocator(base) {
    var n = base || DRILL_DYNAMIC_BASE;
    return function () { return n++; };
  }

  // 给定 send(obj),返回完整 htscene 命令门面。
  function createScene(send) {
    if (typeof send !== 'function')
      throw new Error('createScene(send): 需要一个 send(obj) 函数');

    // 标准要素门面:add 返回 id;update/remove 按 id 寻址。
    // 自动 id 从高号段起,避开编辑器稳定 id(显式传 element.id 时用它)。
    var alloc = makeDynamicIdAllocator(DRILL_DYNAMIC_BASE);
    var mod = function (ns) {
      return {
        add: function (el) {
          el = el || {};
          var id = el.id || alloc();
          send({ ns: ns, op: 'add', id: id, element: Object.assign({}, el, { id: id }) });
          return id;
        },
        update: function (id, el) {
          send({ ns: ns, op: 'update', id: id, element: Object.assign({}, el || {}, { id: id }) });
        },
        remove: function (id) { send({ ns: ns, op: 'remove', id: id }); },
      };
    };

    return {
      // 动态标绘四件套:add({mode,vertices,style,layer_id}) → id。
      polyline: mod('polyline'),
      polygon:  mod('polygon'),
      marker:   mod('marker'),   // 服务端 kind=symbol
      arrow:    mod('arrow'),
      // 运行时模型 / 火苗:add 新建;update 移动/换参。
      model:    mod('model'),
      fire:     mod('fire'),

      // 预加载工程实体:用编辑器【稳定 id】寻址"已经摆好的"model/fire。
      // 静态模型几何已烘焙 → 只能改变换 + 显隐(hideModel=隐藏,非删几何)。
      entity: {
        moveModel: function (id, t) { send({ ns: 'model', op: 'update', id: id, element: Object.assign({}, t || {}, { id: id }) }); },
        showModel: function (id)    { send({ ns: 'model', op: 'update', id: id, element: { id: id } }); },
        hideModel: function (id)    { send({ ns: 'model', op: 'remove', id: id }); },
        moveFire:  function (id, t) { send({ ns: 'fire',  op: 'update', id: id, element: Object.assign({}, t || {}, { id: id }) }); },
        removeFire:function (id)    { send({ ns: 'fire',  op: 'remove', id: id }); },
      },

      layer: {
        show: function (id) { send({ ns: 'layer', op: 'show', id: id }); },
        hide: function (id) { send({ ns: 'layer', op: 'hide', id: id }); },
      },

      camera: {
        orbit: function (dx, dy) { send({ t: 'orbit', dx: dx, dy: dy }); },
        pan:   function (dx, dy) { send({ t: 'pan',   dx: dx, dy: dy }); },
        zoom:  function (d)      { send({ t: 'zoom',  d: d }); },
        reset: function ()       { send({ t: 'reset' }); },
      },

      env: function (p) { send(Object.assign({ ns: 'environment' }, p)); },
      clear: function () { send({ ns: 'scene', op: 'clear' }); },
      raw: function (obj) { send(obj); },
    };
  }

  return {
    createScene: createScene,
    makeDynamicIdAllocator: makeDynamicIdAllocator,
    DRILL_DYNAMIC_BASE: DRILL_DYNAMIC_BASE,
    version: '1.0.0',
  };
});
