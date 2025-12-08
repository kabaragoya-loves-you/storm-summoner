/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  form_control_default
} from "./chunk.E2QYHS4N.js";
import {
  WebAwesomeFormAssociatedElement
} from "./chunk.D4WZJ7B2.js";
import {
  size_default
} from "./chunk.GY3LNU3J.js";
import {
  n,
  o,
  r,
  t
} from "./chunk.7F6EHFYD.js";
import {
  x
} from "./chunk.23WMFJHA.js";
import {
  __decorateClass
} from "./chunk.6E4D3PD7.js";

// src/components/radio/radio.css
var radio_default = ":host {\n  --checked-icon-color: var(--wa-form-control-activated-color);\n  --checked-icon-scale: 0.7;\n\n  color: var(--wa-form-control-value-color);\n  display: inline-flex;\n  flex-direction: row;\n  align-items: top;\n  font-family: inherit;\n  font-weight: var(--wa-form-control-value-font-weight);\n  line-height: var(--wa-form-control-value-line-height);\n  cursor: pointer;\n  user-select: none;\n  -webkit-user-select: none;\n}\n\n:host(:focus) {\n  outline: none;\n}\n\n/* When the control isn't checked, hide the circle for Windows High Contrast mode a11y */\n:host(:not(:state(checked))) svg circle {\n  opacity: 0;\n}\n\n[part~='label'] {\n  display: inline;\n}\n\n[part~='hint'] {\n  margin-block-start: 0.5em;\n}\n\n/* Default spacing for default appearance radios */\n:host([appearance='default']) {\n  margin-block: 0.375em; /* Half of the original 0.75em gap on each side */\n}\n\n:host([appearance='default'][data-wa-radio-horizontal]) {\n  margin-block: 0;\n  margin-inline: 0.5em; /* Half of the original 1em gap on each side */\n}\n\n/* Remove margin from first/last items to prevent extra space */\n:host([appearance='default'][data-wa-radio-first]) {\n  margin-block-start: 0;\n  margin-inline-start: 0;\n}\n\n:host([appearance='default'][data-wa-radio-last]) {\n  margin-block-end: 0;\n  margin-inline-end: 0;\n}\n\n/* Button appearance have no spacing, they get handled by the overlap margins below */\n:host([appearance='button']) {\n  margin: 0;\n  align-items: center;\n  min-height: var(--wa-form-control-height);\n  background-color: var(--wa-color-surface-default);\n  border: var(--wa-form-control-border-width) var(--wa-form-control-border-style) var(--wa-form-control-border-color);\n  border-radius: var(--wa-border-radius-m);\n  padding: 0 var(--wa-form-control-padding-inline);\n  transition:\n    background-color var(--wa-transition-fast),\n    border-color var(--wa-transition-fast);\n}\n\n/* Default appearance */\n:host([appearance='default']) {\n  .control {\n    flex: 0 0 auto;\n    position: relative;\n    display: inline-flex;\n    align-items: center;\n    justify-content: center;\n    width: var(--wa-form-control-toggle-size);\n    height: var(--wa-form-control-toggle-size);\n    border-color: var(--wa-form-control-border-color);\n    border-radius: 50%;\n    border-style: var(--wa-form-control-border-style);\n    border-width: var(--wa-form-control-border-width);\n    background-color: var(--wa-form-control-background-color);\n    color: transparent;\n    transition:\n      background var(--wa-transition-normal),\n      border-color var(--wa-transition-fast),\n      box-shadow var(--wa-transition-fast),\n      color var(--wa-transition-fast);\n    transition-timing-function: var(--wa-transition-easing);\n\n    margin-inline-end: 0.5em;\n  }\n\n  .checked-icon {\n    display: flex;\n    fill: currentColor;\n    width: var(--wa-form-control-toggle-size);\n    height: var(--wa-form-control-toggle-size);\n    scale: var(--checked-icon-scale);\n  }\n}\n\n/* Button appearance */\n:host([appearance='button']) {\n  .control {\n    display: none;\n  }\n}\n\n/* Checked */\n:host(:state(checked)) .control {\n  color: var(--checked-icon-color);\n  border-color: var(--wa-form-control-activated-color);\n  background-color: var(--wa-form-control-background-color);\n}\n\n/* Focus */\n:host(:focus-visible) .control {\n  outline: var(--wa-focus-ring);\n  outline-offset: var(--wa-focus-ring-offset);\n}\n\n/* Disabled */\n:host(:state(disabled)) {\n  opacity: 0.5;\n  cursor: not-allowed;\n}\n\n/* Horizontal grouping - remove inner border radius */\n:host([appearance='button'][data-wa-radio-horizontal][data-wa-radio-inner]) {\n  border-radius: 0;\n}\n\n:host([appearance='button'][data-wa-radio-horizontal][data-wa-radio-first]) {\n  border-start-end-radius: 0;\n  border-end-end-radius: 0;\n}\n\n:host([appearance='button'][data-wa-radio-horizontal][data-wa-radio-last]) {\n  border-start-start-radius: 0;\n  border-end-start-radius: 0;\n}\n\n/* Vertical grouping - remove inner border radius */\n:host([appearance='button'][data-wa-radio-vertical][data-wa-radio-inner]) {\n  border-radius: 0;\n}\n\n:host([appearance='button'][data-wa-radio-vertical][data-wa-radio-first]) {\n  border-end-start-radius: 0;\n  border-end-end-radius: 0;\n}\n\n:host([appearance='button'][data-wa-radio-vertical][data-wa-radio-last]) {\n  border-start-start-radius: 0;\n  border-start-end-radius: 0;\n}\n\n@media (hover: hover) {\n  :host([appearance='button']:hover:not(:state(disabled), :state(checked))) {\n    background-color: color-mix(in srgb, var(--wa-color-surface-default) 95%, var(--wa-color-mix-hover));\n  }\n}\n\n:host([appearance='button']:focus-visible) {\n  outline: var(--wa-focus-ring);\n  outline-offset: var(--wa-focus-ring-offset);\n}\n\n:host([appearance='button']:state(checked)) {\n  border-color: var(--wa-form-control-activated-color);\n  background-color: var(--wa-color-brand-fill-quiet);\n}\n\n:host([appearance='button']:state(checked):focus-visible) {\n  outline: var(--wa-focus-ring-style) var(--wa-focus-ring-width) var(--wa-color-brand-border-loud);\n  outline-offset: var(--wa-focus-ring-offset);\n}\n\n/* Button overlap margins */\n:host([appearance='button'][data-wa-radio-horizontal]:not([data-wa-radio-first])) {\n  margin-inline-start: calc(-1 * var(--wa-form-control-border-width));\n}\n\n:host([appearance='button'][data-wa-radio-vertical]:not([data-wa-radio-first])) {\n  margin-block-start: calc(-1 * var(--wa-form-control-border-width));\n}\n\n/* Ensure interactive states are visible above adjacent buttons */\n:host([appearance='button']:hover),\n:host([appearance='button']:state(checked)) {\n  position: relative;\n  z-index: 1;\n}\n\n:host([appearance='button']:focus-visible) {\n  z-index: 2;\n}\n";

// src/components/radio/radio.ts
var WaRadio = class extends WebAwesomeFormAssociatedElement {
  constructor() {
    super();
    this.checked = false;
    this.forceDisabled = false;
    this.form = null;
    this.appearance = "default";
    this.size = "medium";
    this.disabled = false;
    this.handleClick = () => {
      if (!this.disabled && !this.forceDisabled) {
        this.checked = true;
      }
    };
    if (!o) {
      this.addEventListener("click", this.handleClick);
    }
  }
  connectedCallback() {
    super.connectedCallback();
    this.setInitialAttributes();
  }
  setInitialAttributes() {
    this.setAttribute("role", "radio");
    this.tabIndex = 0;
    this.setAttribute("aria-disabled", this.disabled || this.forceDisabled ? "true" : "false");
  }
  updated(changedProperties) {
    super.updated(changedProperties);
    if (changedProperties.has("checked")) {
      this.customStates.set("checked", this.checked);
      this.setAttribute("aria-checked", this.checked ? "true" : "false");
      if (!this.disabled && !this.forceDisabled) {
        this.tabIndex = this.checked ? 0 : -1;
      }
    }
    if (changedProperties.has("disabled") || changedProperties.has("forceDisabled")) {
      const effectivelyDisabled = this.disabled || this.forceDisabled;
      this.customStates.set("disabled", effectivelyDisabled);
      this.setAttribute("aria-disabled", effectivelyDisabled ? "true" : "false");
      if (effectivelyDisabled) {
        this.tabIndex = -1;
      } else {
        this.tabIndex = this.checked ? 0 : -1;
      }
    }
  }
  /**
   * @override
   */
  setValue() {
  }
  render() {
    return x`
      <span part="control" class="control">
        ${this.checked ? x`
              <svg viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg" part="checked-icon" class="checked-icon">
                <circle cx="8" cy="8" r="8" />
              </svg>
            ` : ""}
      </span>

      <slot part="label" class="label"></slot>
    `;
  }
};
WaRadio.css = [form_control_default, size_default, radio_default];
__decorateClass([
  r()
], WaRadio.prototype, "checked", 2);
__decorateClass([
  r()
], WaRadio.prototype, "forceDisabled", 2);
__decorateClass([
  n({ reflect: true })
], WaRadio.prototype, "form", 2);
__decorateClass([
  n({ reflect: true })
], WaRadio.prototype, "value", 2);
__decorateClass([
  n({ reflect: true })
], WaRadio.prototype, "appearance", 2);
__decorateClass([
  n({ reflect: true })
], WaRadio.prototype, "size", 2);
__decorateClass([
  n({ type: Boolean })
], WaRadio.prototype, "disabled", 2);
WaRadio = __decorateClass([
  t("wa-radio")
], WaRadio);

export {
  WaRadio
};
