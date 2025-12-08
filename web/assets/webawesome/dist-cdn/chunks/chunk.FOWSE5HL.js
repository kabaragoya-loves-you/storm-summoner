/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  l
} from "./chunk.T7SKOEAS.js";
import {
  form_control_default
} from "./chunk.E2QYHS4N.js";
import {
  MirrorValidator
} from "./chunk.47AP2JNT.js";
import {
  WebAwesomeFormAssociatedElement
} from "./chunk.D4WZJ7B2.js";
import {
  o
} from "./chunk.G3G3UFWM.js";
import {
  HasSlotController
} from "./chunk.SFADIYDM.js";
import {
  size_default
} from "./chunk.GY3LNU3J.js";
import {
  e as e2
} from "./chunk.6MY2PWCO.js";
import {
  watch
} from "./chunk.N3AZYXKV.js";
import {
  e,
  n,
  t
} from "./chunk.7F6EHFYD.js";
import {
  x
} from "./chunk.23WMFJHA.js";
import {
  __decorateClass
} from "./chunk.6E4D3PD7.js";

// src/components/switch/switch.css
var switch_default = ":host {\n  --height: var(--wa-form-control-toggle-size);\n  --width: calc(var(--height) * 1.75);\n  --thumb-size: 0.75em;\n\n  display: inline-flex;\n  line-height: var(--wa-form-control-value-line-height);\n}\n\nlabel {\n  position: relative;\n  display: flex;\n  align-items: center;\n  font: inherit;\n  color: var(--wa-form-control-value-color);\n  vertical-align: middle;\n  cursor: pointer;\n}\n\n.switch {\n  flex: 0 0 auto;\n  position: relative;\n  display: flex;\n  align-items: center;\n  justify-content: center;\n  width: var(--width);\n  height: var(--height);\n  background-color: var(--wa-form-control-background-color);\n  border-color: var(--wa-form-control-border-color);\n  border-radius: var(--height);\n  border-style: var(--wa-form-control-border-style);\n  border-width: var(--wa-form-control-border-width);\n  transition-property: translate, background, border-color, box-shadow;\n  transition-duration: var(--wa-transition-normal);\n  transition-timing-function: var(--wa-transition-easing);\n}\n\n.switch .thumb {\n  aspect-ratio: 1 / 1;\n  width: var(--thumb-size);\n  height: var(--thumb-size);\n  background-color: var(--wa-form-control-border-color);\n  border-radius: 50%;\n  translate: calc((var(--width) - var(--height)) / -2);\n  transition: inherit;\n}\n\n.input {\n  position: absolute;\n  opacity: 0;\n  padding: 0;\n  margin: 0;\n  pointer-events: none;\n}\n\n/* Focus */\nlabel:not(.disabled) .input:focus-visible ~ .switch .thumb {\n  outline: var(--wa-focus-ring);\n  outline-offset: var(--wa-focus-ring-offset);\n}\n\n/* Checked */\n.checked .switch {\n  background-color: var(--wa-form-control-activated-color);\n  border-color: var(--wa-form-control-activated-color);\n}\n\n.checked .switch .thumb {\n  background-color: var(--wa-color-surface-default);\n  translate: calc((var(--width) - var(--height)) / 2);\n}\n\n/* Disabled */\nlabel:has(> :disabled) {\n  opacity: 0.5;\n  cursor: not-allowed;\n}\n\n[part~='label'] {\n  display: inline-block;\n  line-height: var(--height);\n  margin-inline-start: 0.5em;\n  user-select: none;\n  -webkit-user-select: none;\n}\n\n:host([required]) [part~='label']::after {\n  content: var(--wa-form-control-required-content);\n  color: var(--wa-form-control-required-content-color);\n  margin-inline-start: var(--wa-form-control-required-content-offset);\n}\n\n@media (forced-colors: active) {\n  :checked:enabled + .switch:hover .thumb,\n  :checked + .switch .thumb {\n    background-color: ButtonText;\n  }\n}\n";

// src/components/switch/switch.ts
var WaSwitch = class extends WebAwesomeFormAssociatedElement {
  constructor() {
    super(...arguments);
    this.hasSlotController = new HasSlotController(this, "hint");
    this.title = "";
    this.name = null;
    this._value = this.getAttribute("value") ?? null;
    this.size = "medium";
    this.disabled = false;
    this.checked = this.hasAttribute("checked");
    this.defaultChecked = this.hasAttribute("checked");
    this.form = null;
    this.required = false;
    this.hint = "";
    this.withHint = false;
  }
  static get validators() {
    return [...super.validators, MirrorValidator()];
  }
  /** The value of the switch, submitted as a name/value pair with form data. */
  get value() {
    return this._value ?? "on";
  }
  set value(val) {
    this._value = val;
  }
  firstUpdated(changedProperties) {
    super.firstUpdated(changedProperties);
    this.handleValueOrCheckedChange();
  }
  handleClick() {
    this.hasInteracted = true;
    this.checked = !this.checked;
    this.updateComplete.then(() => {
      this.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
    });
  }
  handleKeyDown(event) {
    if (event.key === "ArrowLeft") {
      event.preventDefault();
      this.checked = false;
      this.updateComplete.then(() => {
        this.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
        this.dispatchEvent(new InputEvent("input", { bubbles: true, composed: true }));
      });
    }
    if (event.key === "ArrowRight") {
      event.preventDefault();
      this.checked = true;
      this.updateComplete.then(() => {
        this.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
        this.dispatchEvent(new InputEvent("input", { bubbles: true, composed: true }));
      });
    }
  }
  willUpdate(changedProperties) {
    super.willUpdate(changedProperties);
    if (changedProperties.has("defaultChecked")) {
      if (!this.hasInteracted) {
        this.checked = this.defaultChecked;
      }
    }
    if (changedProperties.has("value") || changedProperties.has("checked")) {
      this.handleValueOrCheckedChange();
    }
  }
  handleValueOrCheckedChange() {
    this.setValue(this.checked ? this.value : null, this._value);
    this.updateValidity();
  }
  handleDefaultCheckedChange() {
    if (!this.hasInteracted && this.checked !== this.defaultChecked) {
      this.checked = this.defaultChecked;
      this.handleValueOrCheckedChange();
    }
  }
  handleStateChange() {
    if (this.hasUpdated) {
      this.input.checked = this.checked;
    }
    this.customStates.set("checked", this.checked);
    this.updateValidity();
  }
  handleDisabledChange() {
    this.updateValidity();
  }
  /** Simulates a click on the switch. */
  click() {
    this.input.click();
  }
  /** Sets focus on the switch. */
  focus(options) {
    this.input.focus(options);
  }
  /** Removes focus from the switch. */
  blur() {
    this.input.blur();
  }
  setValue(value, stateValue) {
    if (!this.checked) {
      this.internals.setFormValue(null, null);
      return;
    }
    this.internals.setFormValue(value ?? "on", stateValue);
  }
  formResetCallback() {
    this.checked = this.defaultChecked;
    super.formResetCallback();
    this.handleValueOrCheckedChange();
  }
  render() {
    const hasHintSlot = this.hasUpdated ? this.hasSlotController.test("hint") : this.withHint;
    const hasHint = this.hint ? true : !!hasHintSlot;
    return x`
      <label
        part="base"
        class=${e2({
      checked: this.checked,
      disabled: this.disabled
    })}
      >
        <input
          class="input"
          type="checkbox"
          title=${this.title}
          name=${this.name}
          value=${o(this.value)}
          .checked=${l(this.checked)}
          .disabled=${this.disabled}
          .required=${this.required}
          role="switch"
          aria-checked=${this.checked ? "true" : "false"}
          aria-describedby="hint"
          @click=${this.handleClick}
          @keydown=${this.handleKeyDown}
        />

        <span part="control" class="switch">
          <span part="thumb" class="thumb"></span>
        </span>

        <slot part="label" class="label"></slot>
      </label>

      <slot
        id="hint"
        name="hint"
        part="hint"
        class=${e2({
      "has-slotted": hasHint
    })}
        aria-hidden=${hasHint ? "false" : "true"}
        >${this.hint}</slot
      >
    `;
  }
};
WaSwitch.shadowRootOptions = { ...WebAwesomeFormAssociatedElement.shadowRootOptions, delegatesFocus: true };
WaSwitch.css = [form_control_default, size_default, switch_default];
__decorateClass([
  e('input[type="checkbox"]')
], WaSwitch.prototype, "input", 2);
__decorateClass([
  n()
], WaSwitch.prototype, "title", 2);
__decorateClass([
  n({ reflect: true })
], WaSwitch.prototype, "name", 2);
__decorateClass([
  n({ reflect: true })
], WaSwitch.prototype, "value", 1);
__decorateClass([
  n({ reflect: true })
], WaSwitch.prototype, "size", 2);
__decorateClass([
  n({ type: Boolean })
], WaSwitch.prototype, "disabled", 2);
__decorateClass([
  n({ type: Boolean, attribute: false })
], WaSwitch.prototype, "checked", 2);
__decorateClass([
  n({ type: Boolean, attribute: "checked", reflect: true })
], WaSwitch.prototype, "defaultChecked", 2);
__decorateClass([
  n({ reflect: true })
], WaSwitch.prototype, "form", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaSwitch.prototype, "required", 2);
__decorateClass([
  n({ attribute: "hint" })
], WaSwitch.prototype, "hint", 2);
__decorateClass([
  n({ attribute: "with-hint", type: Boolean })
], WaSwitch.prototype, "withHint", 2);
__decorateClass([
  watch("defaultChecked")
], WaSwitch.prototype, "handleDefaultCheckedChange", 1);
__decorateClass([
  watch(["checked"])
], WaSwitch.prototype, "handleStateChange", 1);
__decorateClass([
  watch("disabled", { waitUntilFirstUpdate: true })
], WaSwitch.prototype, "handleDisabledChange", 1);
WaSwitch = __decorateClass([
  t("wa-switch")
], WaSwitch);

export {
  WaSwitch
};
