/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  o
} from "./chunk.G3G3UFWM.js";
import {
  clamp
} from "./chunk.KABXVFSA.js";
import {
  LocalizeController
} from "./chunk.DR3YY3XN.js";
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

// src/components/progress-bar/progress-bar.css
var progress_bar_default = ":host {\n  --track-height: 1rem;\n  --track-color: var(--wa-color-neutral-fill-normal);\n  --indicator-color: var(--wa-color-brand-fill-loud);\n\n  display: flex;\n}\n\n.progress-bar {\n  flex: 1 1 auto;\n  display: flex;\n  position: relative;\n  overflow: hidden;\n  height: var(--track-height);\n  border-radius: var(--wa-border-radius-pill);\n  background-color: var(--track-color);\n  color: var(--wa-color-brand-on-loud);\n  font-size: var(--wa-font-size-s);\n}\n\n.indicator {\n  width: var(--percentage);\n  display: flex;\n  align-items: center;\n  justify-content: center;\n  background-color: var(--indicator-color);\n  text-align: center;\n  white-space: nowrap;\n  overflow: hidden;\n  line-height: 1;\n  font-weight: var(--wa-font-weight-semibold);\n  transition: all var(--wa-transition-slow, 200ms) var(--wa-transition-easing, ease);\n  user-select: none;\n  -webkit-user-select: none;\n}\n\n/* Indeterminate */\n:host([indeterminate]) .indicator {\n  position: absolute;\n  inset-block: 0;\n  inline-size: 50%;\n  animation: wa-progress-indeterminate 2.5s infinite cubic-bezier(0.37, 0, 0.63, 1);\n}\n\n@media (forced-colors: active) {\n  .progress-bar {\n    outline: solid 1px SelectedItem;\n    background-color: var(--wa-color-surface-default);\n  }\n\n  .indicator {\n    outline: solid 1px SelectedItem;\n    background-color: SelectedItem;\n  }\n}\n\n@keyframes wa-progress-indeterminate {\n  0% {\n    inset-inline-start: -50%;\n  }\n\n  75%,\n  100% {\n    inset-inline-start: 100%;\n  }\n}\n";

// src/components/progress-bar/progress-bar.ts
var WaProgressBar = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.localize = new LocalizeController(this);
    this.value = 0;
    this.indeterminate = false;
    this.label = "";
  }
  updated(changedProperties) {
    if (changedProperties.has("value")) {
      requestAnimationFrame(() => {
        this.style.setProperty("--percentage", `${clamp(this.value, 0, 100)}%`);
      });
    }
  }
  render() {
    return x`
      <div
        part="base"
        class="progress-bar"
        role="progressbar"
        title=${o(this.title)}
        aria-label=${this.label.length > 0 ? this.label : this.localize.term("progress")}
        aria-valuemin="0"
        aria-valuemax="100"
        aria-valuenow=${this.indeterminate ? "0" : this.value}
      >
        <div part="indicator" class="indicator">
          ${!this.indeterminate ? x` <slot part="label" class="label"></slot> ` : ""}
        </div>
      </div>
    `;
  }
};
WaProgressBar.css = progress_bar_default;
__decorateClass([
  n({ type: Number, reflect: true })
], WaProgressBar.prototype, "value", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaProgressBar.prototype, "indeterminate", 2);
__decorateClass([
  n()
], WaProgressBar.prototype, "label", 2);
WaProgressBar = __decorateClass([
  t("wa-progress-bar")
], WaProgressBar);

export {
  WaProgressBar
};
