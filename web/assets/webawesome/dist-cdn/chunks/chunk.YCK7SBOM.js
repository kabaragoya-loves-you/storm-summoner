/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  o,
  require_react
} from "./chunk.G5SSLR4K.js";
import {
  WaPopover
} from "./chunk.OX4CY7TL.js";
import {
  __toESM
} from "./chunk.6E4D3PD7.js";

// src/react/popover/index.ts
var React = __toESM(require_react(), 1);
var tagName = "wa-popover";
var reactWrapper = o({
  tagName,
  elementClass: WaPopover,
  react: React,
  events: {
    onWaShow: "wa-show",
    onWaAfterShow: "wa-after-show",
    onWaHide: "wa-hide",
    onWaAfterHide: "wa-after-hide"
  },
  displayName: "WaPopover"
});
var popover_default = reactWrapper;

export {
  popover_default
};
