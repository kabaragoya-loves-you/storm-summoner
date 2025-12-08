/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */

// src/styles/utilities/visually-hidden.css
var visually_hidden_default = "@layer wa-utilities {\n  .wa-visually-hidden:not(:focus-within),\n  .wa-visually-hidden-force,\n  .wa-visually-hidden-hint::part(hint),\n  .wa-visually-hidden-label::part(label) {\n    position: absolute !important;\n    width: 1px !important;\n    height: 1px !important;\n    clip: rect(0 0 0 0) !important;\n    clip-path: inset(50%) !important;\n    border: none !important;\n    overflow: hidden !important;\n    white-space: nowrap !important;\n    padding: 0 !important;\n  }\n}\n";

export {
  visually_hidden_default
};
