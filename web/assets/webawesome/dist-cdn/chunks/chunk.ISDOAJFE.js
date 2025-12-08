/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  variants_default
} from "./chunk.R6O3GHMM.js";
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

// src/components/badge/badge.css
var badge_default = ":host {\n  --pulse-color: var(--wa-color-fill-loud, var(--wa-color-brand-fill-loud));\n\n  display: inline-flex;\n  align-items: center;\n  justify-content: center;\n  padding: 0.375em 0.625em;\n  color: var(--wa-color-on-loud, var(--wa-color-brand-on-loud));\n  font-size: max(var(--wa-font-size-2xs), 0.75em);\n  font-weight: var(--wa-font-weight-semibold);\n  line-height: 1;\n  white-space: nowrap;\n  background-color: var(--wa-color-fill-loud, var(--wa-color-brand-fill-loud));\n  border-color: transparent;\n  border-radius: var(--wa-border-radius-s);\n  border-style: var(--wa-border-style);\n  border-width: var(--wa-border-width-s);\n  user-select: none;\n  -webkit-user-select: none;\n  cursor: inherit;\n}\n\n/* Appearance modifiers */\n:host([appearance='outlined']) {\n  --pulse-color: var(--wa-color-border-loud, var(--wa-color-brand-border-loud));\n\n  color: var(--wa-color-on-quiet, var(--wa-color-brand-on-quiet));\n  background-color: transparent;\n  border-color: var(--wa-color-border-loud, var(--wa-color-brand-border-loud));\n}\n\n:host([appearance='filled']) {\n  --pulse-color: var(--wa-color-fill-normal, var(--wa-color-brand-fill-normal));\n\n  color: var(--wa-color-on-normal, var(--wa-color-brand-on-normal));\n  background-color: var(--wa-color-fill-normal, var(--wa-color-brand-fill-normal));\n  border-color: transparent;\n}\n\n:host([appearance='filled-outlined']) {\n  --pulse-color: var(--wa-color-border-normal, var(--wa-color-brand-border-normal));\n\n  color: var(--wa-color-on-normal, var(--wa-color-brand-on-normal));\n  background-color: var(--wa-color-fill-normal, var(--wa-color-brand-fill-normal));\n  border-color: var(--wa-color-border-normal, var(--wa-color-brand-border-normal));\n}\n\n:host([appearance='accent']) {\n  --pulse-color: var(--wa-color-fill-loud, var(--wa-color-brand-fill-loud));\n\n  color: var(--wa-color-on-loud, var(--wa-color-brand-on-loud));\n  background-color: var(--wa-color-fill-loud, var(--wa-color-brand-fill-loud));\n  border-color: transparent;\n}\n\n/* Pill modifier */\n:host([pill]) {\n  border-radius: var(--wa-border-radius-pill);\n}\n\n/* Pulse attention */\n:host([attention='pulse']) {\n  animation: pulse 1.5s infinite;\n}\n\n@keyframes pulse {\n  0% {\n    box-shadow: 0 0 0 0 var(--pulse-color);\n  }\n  70% {\n    box-shadow: 0 0 0 0.5rem transparent;\n  }\n  100% {\n    box-shadow: 0 0 0 0 transparent;\n  }\n}\n\n/* Bounce attention */\n:host([attention='bounce']) {\n  animation: bounce 1s cubic-bezier(0.28, 0.84, 0.42, 1) infinite;\n}\n\n@keyframes bounce {\n  0%,\n  20%,\n  50%,\n  80%,\n  100% {\n    transform: translateY(0);\n  }\n  40% {\n    transform: translateY(-5px);\n  }\n  60% {\n    transform: translateY(-2px);\n  }\n}\n\n::slotted(wa-icon) {\n  margin-inline-end: var(--wa-space-2xs, 0.25em);\n  opacity: 90%;\n  line-height: 1;\n  height: 0.85em;\n}\n";

// src/components/badge/badge.ts
var WaBadge = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.variant = "brand";
    this.appearance = "accent";
    this.pill = false;
    this.attention = "none";
  }
  render() {
    return x` <slot part="base" role="status"></slot>`;
  }
};
WaBadge.css = [variants_default, badge_default];
__decorateClass([
  n({ reflect: true })
], WaBadge.prototype, "variant", 2);
__decorateClass([
  n({ reflect: true })
], WaBadge.prototype, "appearance", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaBadge.prototype, "pill", 2);
__decorateClass([
  n({ reflect: true })
], WaBadge.prototype, "attention", 2);
WaBadge = __decorateClass([
  t("wa-badge")
], WaBadge);

export {
  WaBadge
};
