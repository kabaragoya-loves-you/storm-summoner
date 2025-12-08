/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  WaClearEvent
} from "./chunk.U5TEITCX.js";
import {
  submitOnEnter
} from "./chunk.36SP54IM.js";
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
  o as o2
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
  LocalizeController
} from "./chunk.DR3YY3XN.js";
import {
  watch
} from "./chunk.N3AZYXKV.js";
import {
  e,
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

// src/components/input/input.css
var input_default = ":host {\n  border-width: 0;\n}\n\n.text-field {\n  flex: auto;\n  display: flex;\n  align-items: stretch;\n  justify-content: start;\n  position: relative;\n  transition: inherit;\n  height: var(--wa-form-control-height);\n  border-color: var(--wa-form-control-border-color);\n  border-radius: var(--wa-form-control-border-radius);\n  border-style: var(--wa-form-control-border-style);\n  border-width: var(--wa-form-control-border-width);\n  cursor: text;\n  color: var(--wa-form-control-value-color);\n  font-size: var(--wa-form-control-value-font-size);\n  font-family: inherit;\n  font-weight: var(--wa-form-control-value-font-weight);\n  line-height: var(--wa-form-control-value-line-height);\n  vertical-align: middle;\n  width: 100%;\n  transition:\n    background-color var(--wa-transition-normal),\n    border var(--wa-transition-normal),\n    outline var(--wa-transition-fast);\n  transition-timing-function: var(--wa-transition-easing);\n  background-color: var(--wa-form-control-background-color);\n  box-shadow: var(--box-shadow);\n  padding: 0 var(--wa-form-control-padding-inline);\n\n  &:focus-within {\n    outline: var(--wa-focus-ring);\n    outline-offset: var(--wa-focus-ring-offset);\n  }\n\n  /* Style disabled inputs */\n  &:has(:disabled) {\n    cursor: not-allowed;\n    opacity: 0.5;\n  }\n}\n\n/* Appearance modifiers */\n:host([appearance='outlined']) .text-field {\n  background-color: var(--wa-form-control-background-color);\n  border-color: var(--wa-form-control-border-color);\n}\n\n:host([appearance='filled']) .text-field {\n  background-color: var(--wa-color-neutral-fill-quiet);\n  border-color: var(--wa-color-neutral-fill-quiet);\n}\n\n:host([appearance='filled-outlined']) .text-field {\n  background-color: var(--wa-color-neutral-fill-quiet);\n  border-color: var(--wa-form-control-border-color);\n}\n\n:host([pill]) .text-field {\n  border-radius: var(--wa-border-radius-pill) !important;\n}\n\n.text-field {\n  /* Show autofill styles over the entire text field, not just the native <input> */\n  &:has(:autofill),\n  &:has(:-webkit-autofill) {\n    background-color: var(--wa-color-brand-fill-quiet) !important;\n  }\n\n  input,\n  textarea {\n    /*\n    Fixes an alignment issue with placeholders.\n    https://github.com/shoelace-style/webawesome/issues/342\n  */\n    height: 100%;\n\n    padding: 0;\n    border: none;\n    outline: none;\n    box-shadow: none;\n    margin: 0;\n    cursor: inherit;\n    -webkit-appearance: none;\n    font: inherit;\n\n    /* Turn off Safari's autofill styles */\n    &:-webkit-autofill,\n    &:-webkit-autofill:hover,\n    &:-webkit-autofill:focus,\n    &:-webkit-autofill:active {\n      -webkit-background-clip: text;\n      background-color: transparent;\n      -webkit-text-fill-color: inherit;\n    }\n  }\n}\n\ninput {\n  flex: 1 1 auto;\n  min-width: 0;\n  height: 100%;\n  transition: inherit;\n\n  /* prettier-ignore */\n  background-color: rgb(118 118 118 / 0); /* ensures proper placeholder styles in webkit's date input */\n  height: calc(var(--wa-form-control-height) - var(--border-width) * 2);\n  padding-block: 0;\n  color: inherit;\n\n  &:autofill {\n    &,\n    &:hover,\n    &:focus,\n    &:active {\n      box-shadow: none;\n      caret-color: var(--wa-form-control-value-color);\n    }\n  }\n\n  &::placeholder {\n    color: var(--wa-form-control-placeholder-color);\n    user-select: none;\n    -webkit-user-select: none;\n  }\n\n  &::-webkit-search-decoration,\n  &::-webkit-search-cancel-button,\n  &::-webkit-search-results-button,\n  &::-webkit-search-results-decoration {\n    -webkit-appearance: none;\n  }\n\n  &:focus {\n    outline: none;\n  }\n}\n\ntextarea {\n  &:autofill {\n    &,\n    &:hover,\n    &:focus,\n    &:active {\n      box-shadow: none;\n      caret-color: var(--wa-form-control-value-color);\n    }\n  }\n\n  &::placeholder {\n    color: var(--wa-form-control-placeholder-color);\n    user-select: none;\n    -webkit-user-select: none;\n  }\n}\n\n.start,\n.end {\n  display: inline-flex;\n  flex: 0 0 auto;\n  align-items: center;\n  cursor: default;\n\n  &::slotted(wa-icon) {\n    color: var(--wa-color-neutral-on-quiet);\n  }\n}\n\n.start::slotted(*) {\n  margin-inline-end: var(--wa-form-control-padding-inline);\n}\n\n.end::slotted(*) {\n  margin-inline-start: var(--wa-form-control-padding-inline);\n}\n\n/*\n * Clearable + Password Toggle\n */\n\n.clear,\n.password-toggle {\n  display: inline-flex;\n  align-items: center;\n  justify-content: center;\n  font-size: inherit;\n  color: var(--wa-color-neutral-on-quiet);\n  border: none;\n  background: none;\n  padding: 0;\n  transition: var(--wa-transition-normal) color;\n  cursor: pointer;\n  margin-inline-start: var(--wa-form-control-padding-inline);\n\n  @media (hover: hover) {\n    &:hover {\n      color: color-mix(in oklab, currentColor, var(--wa-color-mix-hover));\n    }\n  }\n\n  &:active {\n    color: color-mix(in oklab, currentColor, var(--wa-color-mix-active));\n  }\n\n  &:focus {\n    outline: none;\n  }\n}\n\n/* Don't show the browser's password toggle in Edge */\n::-ms-reveal {\n  display: none;\n}\n\n/* Hide the built-in number spinner */\n:host([without-spin-buttons]) input[type='number'] {\n  -moz-appearance: textfield;\n\n  &::-webkit-outer-spin-button,\n  &::-webkit-inner-spin-button {\n    -webkit-appearance: none;\n    display: none;\n  }\n}\n";

// src/components/input/input.ts
var WaInput = class extends WebAwesomeFormAssociatedElement {
  constructor() {
    super(...arguments);
    this.assumeInteractionOn = ["blur", "input"];
    this.hasSlotController = new HasSlotController(this, "hint", "label");
    this.localize = new LocalizeController(this);
    this.title = "";
    this.type = "text";
    this._value = null;
    this.defaultValue = this.getAttribute("value") || null;
    this.size = "medium";
    this.appearance = "outlined";
    this.pill = false;
    this.label = "";
    this.hint = "";
    this.withClear = false;
    this.placeholder = "";
    this.readonly = false;
    this.passwordToggle = false;
    this.passwordVisible = false;
    this.withoutSpinButtons = false;
    this.form = null;
    this.required = false;
    this.spellcheck = true;
    this.withLabel = false;
    this.withHint = false;
  }
  static get validators() {
    return [...super.validators, MirrorValidator()];
  }
  /** The current value of the input, submitted as a name/value pair with form data. */
  get value() {
    if (this.valueHasChanged) {
      return this._value;
    }
    return this._value ?? this.defaultValue;
  }
  set value(val) {
    if (this._value === val) {
      return;
    }
    this.valueHasChanged = true;
    this._value = val;
  }
  handleChange(event) {
    this.value = this.input.value;
    this.relayNativeEvent(event, { bubbles: true, composed: true });
  }
  handleClearClick(event) {
    event.preventDefault();
    if (this.value !== "") {
      this.value = "";
      this.updateComplete.then(() => {
        this.dispatchEvent(new WaClearEvent());
        this.dispatchEvent(new InputEvent("input", { bubbles: true, composed: true }));
        this.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
      });
    }
    this.input.focus();
  }
  handleInput() {
    this.value = this.input.value;
  }
  handleKeyDown(event) {
    submitOnEnter(event, this);
  }
  handlePasswordToggle() {
    this.passwordVisible = !this.passwordVisible;
  }
  updated(changedProperties) {
    super.updated(changedProperties);
    if (changedProperties.has("value")) {
      this.customStates.set("blank", !this.value);
    }
  }
  handleStepChange() {
    this.input.step = String(this.step);
    this.updateValidity();
  }
  /** Sets focus on the input. */
  focus(options) {
    this.input.focus(options);
  }
  /** Removes focus from the input. */
  blur() {
    this.input.blur();
  }
  /** Selects all the text in the input. */
  select() {
    this.input.select();
  }
  /** Sets the start and end positions of the text selection (0-based). */
  setSelectionRange(selectionStart, selectionEnd, selectionDirection = "none") {
    this.input.setSelectionRange(selectionStart, selectionEnd, selectionDirection);
  }
  /** Replaces a range of text with a new string. */
  setRangeText(replacement, start, end, selectMode = "preserve") {
    const selectionStart = start ?? this.input.selectionStart;
    const selectionEnd = end ?? this.input.selectionEnd;
    this.input.setRangeText(replacement, selectionStart, selectionEnd, selectMode);
    if (this.value !== this.input.value) {
      this.value = this.input.value;
    }
  }
  /** Displays the browser picker for an input element (only works if the browser supports it for the input type). */
  showPicker() {
    if ("showPicker" in HTMLInputElement.prototype) {
      this.input.showPicker();
    }
  }
  /** Increments the value of a numeric input type by the value of the step attribute. */
  stepUp() {
    this.input.stepUp();
    if (this.value !== this.input.value) {
      this.value = this.input.value;
    }
  }
  /** Decrements the value of a numeric input type by the value of the step attribute. */
  stepDown() {
    this.input.stepDown();
    if (this.value !== this.input.value) {
      this.value = this.input.value;
    }
  }
  formResetCallback() {
    this.value = this.defaultValue;
    super.formResetCallback();
  }
  render() {
    const hasLabelSlot = this.hasUpdated ? this.hasSlotController.test("label") : this.withLabel;
    const hasHintSlot = this.hasUpdated ? this.hasSlotController.test("hint") : this.withHint;
    const hasLabel = this.label ? true : !!hasLabelSlot;
    const hasHint = this.hint ? true : !!hasHintSlot;
    const hasClearIcon = this.withClear && !this.disabled && !this.readonly;
    const isClearIconVisible = (
      // prevents hydration mismatch errors.
      (o || this.hasUpdated) && hasClearIcon && (typeof this.value === "number" || this.value && this.value.length > 0)
    );
    return x`
      <label part="form-control-label label" class="label" for="input" aria-hidden=${hasLabel ? "false" : "true"}>
        <slot name="label">${this.label}</slot>
      </label>

      <div part="base" class="text-field">
        <slot name="start" part="start" class="start"></slot>

        <input
          part="input"
          id="input"
          class="control"
          type=${this.type === "password" && this.passwordVisible ? "text" : this.type}
          title=${this.title}
          name=${o2(this.name)}
          ?disabled=${this.disabled}
          ?readonly=${this.readonly}
          ?required=${this.required}
          placeholder=${o2(this.placeholder)}
          minlength=${o2(this.minlength)}
          maxlength=${o2(this.maxlength)}
          min=${o2(this.min)}
          max=${o2(this.max)}
          step=${o2(this.step)}
          .value=${l(this.value ?? "")}
          autocapitalize=${o2(this.autocapitalize)}
          autocomplete=${o2(this.autocomplete)}
          autocorrect=${o2(this.autocorrect)}
          ?autofocus=${this.autofocus}
          spellcheck=${this.spellcheck}
          pattern=${o2(this.pattern)}
          enterkeyhint=${o2(this.enterkeyhint)}
          inputmode=${o2(this.inputmode)}
          aria-describedby="hint"
          @change=${this.handleChange}
          @input=${this.handleInput}
          @keydown=${this.handleKeyDown}
        />

        ${isClearIconVisible ? x`
              <button
                part="clear-button"
                class="clear"
                type="button"
                aria-label=${this.localize.term("clearEntry")}
                @click=${this.handleClearClick}
                tabindex="-1"
              >
                <slot name="clear-icon">
                  <wa-icon name="circle-xmark" library="system" variant="regular"></wa-icon>
                </slot>
              </button>
            ` : ""}
        ${this.passwordToggle && !this.disabled ? x`
              <button
                part="password-toggle-button"
                class="password-toggle"
                type="button"
                aria-label=${this.localize.term(this.passwordVisible ? "hidePassword" : "showPassword")}
                @click=${this.handlePasswordToggle}
                tabindex="-1"
              >
                ${!this.passwordVisible ? x`
                      <slot name="show-password-icon">
                        <wa-icon name="eye" library="system" variant="regular"></wa-icon>
                      </slot>
                    ` : x`
                      <slot name="hide-password-icon">
                        <wa-icon name="eye-slash" library="system" variant="regular"></wa-icon>
                      </slot>
                    `}
              </button>
            ` : ""}

        <slot name="end" part="end" class="end"></slot>
      </div>

      <slot
        id="hint"
        part="hint"
        name="hint"
        class=${e2({
      "has-slotted": hasHint
    })}
        aria-hidden=${hasHint ? "false" : "true"}
        >${this.hint}</slot
      >
    `;
  }
};
WaInput.css = [size_default, form_control_default, input_default];
WaInput.shadowRootOptions = { ...WebAwesomeFormAssociatedElement.shadowRootOptions, delegatesFocus: true };
__decorateClass([
  e("input")
], WaInput.prototype, "input", 2);
__decorateClass([
  n()
], WaInput.prototype, "title", 2);
__decorateClass([
  n({ reflect: true })
], WaInput.prototype, "type", 2);
__decorateClass([
  r()
], WaInput.prototype, "value", 1);
__decorateClass([
  n({ attribute: "value", reflect: true })
], WaInput.prototype, "defaultValue", 2);
__decorateClass([
  n({ reflect: true })
], WaInput.prototype, "size", 2);
__decorateClass([
  n({ reflect: true })
], WaInput.prototype, "appearance", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaInput.prototype, "pill", 2);
__decorateClass([
  n()
], WaInput.prototype, "label", 2);
__decorateClass([
  n({ attribute: "hint" })
], WaInput.prototype, "hint", 2);
__decorateClass([
  n({ attribute: "with-clear", type: Boolean })
], WaInput.prototype, "withClear", 2);
__decorateClass([
  n()
], WaInput.prototype, "placeholder", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaInput.prototype, "readonly", 2);
__decorateClass([
  n({ attribute: "password-toggle", type: Boolean })
], WaInput.prototype, "passwordToggle", 2);
__decorateClass([
  n({ attribute: "password-visible", type: Boolean })
], WaInput.prototype, "passwordVisible", 2);
__decorateClass([
  n({ attribute: "without-spin-buttons", type: Boolean })
], WaInput.prototype, "withoutSpinButtons", 2);
__decorateClass([
  n({ reflect: true })
], WaInput.prototype, "form", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaInput.prototype, "required", 2);
__decorateClass([
  n()
], WaInput.prototype, "pattern", 2);
__decorateClass([
  n({ type: Number })
], WaInput.prototype, "minlength", 2);
__decorateClass([
  n({ type: Number })
], WaInput.prototype, "maxlength", 2);
__decorateClass([
  n()
], WaInput.prototype, "min", 2);
__decorateClass([
  n()
], WaInput.prototype, "max", 2);
__decorateClass([
  n()
], WaInput.prototype, "step", 2);
__decorateClass([
  n()
], WaInput.prototype, "autocapitalize", 2);
__decorateClass([
  n()
], WaInput.prototype, "autocorrect", 2);
__decorateClass([
  n()
], WaInput.prototype, "autocomplete", 2);
__decorateClass([
  n({ type: Boolean })
], WaInput.prototype, "autofocus", 2);
__decorateClass([
  n()
], WaInput.prototype, "enterkeyhint", 2);
__decorateClass([
  n({
    type: Boolean,
    converter: {
      // Allow "true|false" attribute values but keep the property boolean
      fromAttribute: (value) => !value || value === "false" ? false : true,
      toAttribute: (value) => value ? "true" : "false"
    }
  })
], WaInput.prototype, "spellcheck", 2);
__decorateClass([
  n()
], WaInput.prototype, "inputmode", 2);
__decorateClass([
  n({ attribute: "with-label", type: Boolean })
], WaInput.prototype, "withLabel", 2);
__decorateClass([
  n({ attribute: "with-hint", type: Boolean })
], WaInput.prototype, "withHint", 2);
__decorateClass([
  watch("step", { waitUntilFirstUpdate: true })
], WaInput.prototype, "handleStepChange", 1);
WaInput = __decorateClass([
  t("wa-input")
], WaInput);

export {
  WaInput
};
