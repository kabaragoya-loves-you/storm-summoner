/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  o,
  require_react
} from "./chunk.G5SSLR4K.js";
import {
  WaDialog
} from "./chunk.WAIKKDXO.js";
import {
  __toESM
} from "./chunk.6E4D3PD7.js";

// src/react/dialog/index.ts
var React = __toESM(require_react(), 1);
var tagName = "wa-dialog";
var reactWrapper = o({
  tagName,
  elementClass: WaDialog,
  react: React,
  events: {
    onWaShow: "wa-show",
    onWaAfterShow: "wa-after-show",
    onWaHide: "wa-hide",
    onWaAfterHide: "wa-after-hide"
  },
  displayName: "WaDialog"
});
var dialog_default = reactWrapper;

export {
  dialog_default
};
