# QuickShiori

一个基于 [QuickJS](https://github.com/quickjs-ng/quickjs) 并经过部分修改的 JavaScript 运行时，为 Ukagaka（伺か）平台提供现代化的脚本支持。

## 警告

**This project is unstable and primarily a VIBE CODING product. Use with caution in production environments.**

**警告：本项目尚处于不稳定状态，大多数产物为VIBE CODING。请勿直接用于生产环境，风险自担。**

**STILL IN BETA**

## 项目简介

QuickShiori 是一个实现了 SHIORI/SAORI 协议的 DLL 容器，允许你使用 JavaScript 编写 Ukagaka 人格的脚本逻辑。它将 QuickJS 引擎封装为标准的 Ukagaka DLL 接口，使传统的 Ukagaka 基础软件能够加载和执行现代 JavaScript 代码。

本项目类似于Kagari，仅为cpp桥接层与部分用着合适的quickjs原生模块

## 使用说明
参见 quickshiori.d.ts

本品使用Cmake构建，将构建产物的 `qjs.dll` 和 `quickshiori.dll` 复制到ghost文件夹，并创建一个 `index.js`

内容如下
```javascript
globalThis.__shiori_load = function (dir) {

};

globalThis.__shiori_request = function (rawRequest) {

};

globalThis.__shiori_unload = function () {

};
```

实现好函数即可使用。

## SAORI支持
参见 [ukadll.d.js](./module/ukadll/ukadll.d.ts) 和 [saori.js](./module/ukadll/saori.js) 

## 许可证
参见LICENSE文件
