/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  variants_default
} from "./chunk.R6O3GHMM.js";
import {
  size_default
} from "./chunk.GY3LNU3J.js";
import {
  WebAwesomeElement,
  n,
  t
} from "./chunk.7F6EHFYD.js";
import {
  x
} from "./chunk.23WMFJHA.js";
import {
  __decorateClass
} from "./chunk.6E4D3PD7.js";

// src/components/callout/callout.css
var callout_default = ":host {\n  display: flex;\n  position: relative;\n  align-items: stretch;\n  border-radius: var(--wa-panel-border-radius);\n  background-color: var(--wa-color-fill-quiet, var(--wa-color-brand-fill-quiet));\n  border-color: var(--wa-color-border-quiet, var(--wa-color-brand-border-quiet));\n  border-style: var(--wa-panel-border-style);\n  border-width: var(--wa-panel-border-width);\n  color: var(--wa-color-text-normal);\n  padding: 1em;\n}\n\n/* Appearance modifiers */\n:host([appearance~='plain']) {\n  background-color: transparent;\n  border-color: transparent;\n}\n\n:host([appearance~='outlined']) {\n  background-color: transparent;\n  border-color: var(--wa-color-border-loud, var(--wa-color-brand-border-loud));\n}\n\n:host([appearance~='filled']) {\n  background-color: var(--wa-color-fill-quiet, var(--wa-color-brand-fill-quiet));\n  border-color: transparent;\n}\n\n:host([appearance~='filled-outlined']) {\n  border-color: var(--wa-color-border-quiet, var(--wa-color-brand-border-quiet));\n}\n\n:host([appearance~='accent']) {\n  color: var(--wa-color-on-loud, var(--wa-color-brand-on-loud));\n  background-color: var(--wa-color-fill-loud, var(--wa-color-brand-fill-loud));\n  border-color: transparent;\n\n  [part~='icon'] {\n    color: currentColor;\n  }\n}\n\n[part~='icon'] {\n  flex: 0 0 auto;\n  display: flex;\n  align-items: center;\n  color: var(--wa-color-on-quiet);\n  font-size: 1.25em;\n}\n\n::slotted([slot='icon']) {\n  margin-inline-end: var(--wa-form-control-padding-inline);\n}\n\n[part~='message'] {\n  flex: 1 1 auto;\n  display: block;\n  overflow: hidden;\n}\n";

// src/components/callout/callout.ts
var WaCallout = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.variant = "brand";
    this.size = "medium";
  }
  render() {
    return x`
      <div part="icon">
        <slot name="icon"></slot>
      </div>

      <div part="message">
        <slot></slot>
      </div>
    `;
  }
};
WaCallout.css = [callout_default, variants_default, size_default];
__decorateClass([
  n({ reflect: true })
], WaCallout.prototype, "variant", 2);
__decorateClass([
  n({ reflect: true })
], WaCallout.prototype, "appearance", 2);
__decorateClass([
  n({ reflect: true })
], WaCallout.prototype, "size", 2);
WaCallout = __decorateClass([
  t("wa-callout")
], WaCallout);

export {
  WaCallout
};
