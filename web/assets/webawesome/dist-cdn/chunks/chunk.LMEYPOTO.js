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
  r,
  t
} from "./chunk.7F6EHFYD.js";
import {
  x
} from "./chunk.23WMFJHA.js";
import {
  __decorateClass
} from "./chunk.6E4D3PD7.js";

// src/components/textarea/textarea.css
var textarea_default = ":host {\n  border-width: 0;\n}\n\n.textarea {\n  display: grid;\n  align-items: center;\n  margin: 0;\n  border: none;\n  outline: none;\n  cursor: inherit;\n  font: inherit;\n  background-color: var(--wa-form-control-background-color);\n  border-color: var(--wa-form-control-border-color);\n  border-radius: var(--wa-form-control-border-radius);\n  border-style: var(--wa-form-control-border-style);\n  border-width: var(--wa-form-control-border-width);\n  -webkit-appearance: none;\n\n  &:focus-within {\n    outline: var(--wa-focus-ring);\n    outline-offset: var(--wa-focus-ring-offset);\n  }\n}\n\n/* Appearance modifiers */\n:host([appearance='outlined']) .textarea {\n  background-color: var(--wa-form-control-background-color);\n  border-color: var(--wa-form-control-border-color);\n}\n\n:host([appearance='filled']) .textarea {\n  background-color: var(--wa-color-neutral-fill-quiet);\n  border-color: var(--wa-color-neutral-fill-quiet);\n}\n\n:host([appearance='filled-outlined']) .textarea {\n  background-color: var(--wa-color-neutral-fill-quiet);\n  border-color: var(--wa-form-control-border-color);\n}\n\ntextarea {\n  display: block;\n  width: 100%;\n  border: none;\n  background: transparent;\n  font: inherit;\n  color: inherit;\n  padding: calc(var(--wa-form-control-padding-block) - ((1lh - 1em) / 2)) var(--wa-form-control-padding-inline); /* accounts for the larger line height of textarea content */\n  min-height: calc(var(--wa-form-control-height) - var(--border-width) * 2);\n  box-shadow: none;\n  margin: 0;\n\n  &::placeholder {\n    color: var(--wa-form-control-placeholder-color);\n    user-select: none;\n    -webkit-user-select: none;\n  }\n\n  &:autofill {\n    &,\n    &:hover,\n    &:focus,\n    &:active {\n      box-shadow: none;\n      caret-color: var(--wa-form-control-value-color);\n    }\n  }\n\n  &:focus {\n    outline: none;\n  }\n}\n\n/* Shared textarea and size-adjuster positioning */\n.control,\n.size-adjuster {\n  grid-area: 1 / 1 / 2 / 2;\n}\n\n.size-adjuster {\n  visibility: hidden;\n  pointer-events: none;\n  opacity: 0;\n  padding: 0;\n}\n\ntextarea::-webkit-search-decoration,\ntextarea::-webkit-search-cancel-button,\ntextarea::-webkit-search-results-button,\ntextarea::-webkit-search-results-decoration {\n  -webkit-appearance: none;\n}\n\n/*\n * Resize types\n */\n\n:host([resize='none']) textarea {\n  resize: none;\n}\n\ntextarea,\n:host([resize='vertical']) textarea {\n  resize: vertical;\n}\n\n:host([resize='horizontal']) textarea {\n  resize: horizontal;\n}\n\n:host([resize='both']) textarea {\n  resize: both;\n}\n\n:host([resize='auto']) textarea {\n  height: auto;\n  resize: none;\n  overflow-y: hidden;\n}\n";

// src/components/textarea/textarea.ts
var WaTextarea = class extends WebAwesomeFormAssociatedElement {
  constructor() {
    super(...arguments);
    this.assumeInteractionOn = ["blur", "input"];
    this.hasSlotController = new HasSlotController(this, "hint", "label");
    this.title = "";
    this.name = null;
    this._value = null;
    this.defaultValue = this.getAttribute("value") ?? "";
    this.size = "medium";
    this.appearance = "outlined";
    this.label = "";
    this.hint = "";
    this.placeholder = "";
    this.rows = 4;
    this.resize = "vertical";
    this.disabled = false;
    this.readonly = false;
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
  connectedCallback() {
    super.connectedCallback();
    this.resizeObserver = new ResizeObserver(() => this.setTextareaDimensions());
    this.updateComplete.then(() => {
      this.setTextareaDimensions();
      this.resizeObserver.observe(this.input);
      if (this.didSSR && this.input && this.value !== this.input.value) {
        const value = this.input.value;
        this.value = value;
      }
    });
  }
  disconnectedCallback() {
    super.disconnectedCallback();
    if (this.input) {
      this.resizeObserver?.unobserve(this.input);
    }
  }
  handleBlur() {
    this.checkValidity();
  }
  handleChange(event) {
    this.valueHasChanged = true;
    this.value = this.input.value;
    this.setTextareaDimensions();
    this.checkValidity();
    this.relayNativeEvent(event, { bubbles: true, composed: true });
  }
  handleInput(event) {
    this.valueHasChanged = true;
    this.value = this.input.value;
    this.relayNativeEvent(event, { bubbles: true, composed: true });
  }
  setTextareaDimensions() {
    if (this.resize === "none") {
      this.base.style.width = ``;
      this.base.style.height = ``;
      return;
    }
    if (this.resize === "auto") {
      this.sizeAdjuster.style.height = `${this.input.clientHeight}px`;
      this.input.style.height = "auto";
      this.input.style.height = `${this.input.scrollHeight}px`;
      this.base.style.width = ``;
      this.base.style.height = ``;
      return;
    }
    if (this.input.style.width) {
      const width = Number(this.input.style.width.split(/px/)[0]) + 2;
      this.base.style.width = `${width}px`;
    }
    if (this.input.style.height) {
      const height = Number(this.input.style.height.split(/px/)[0]) + 2;
      this.base.style.height = `${height}px`;
    }
  }
  handleRowsChange() {
    this.setTextareaDimensions();
  }
  async handleValueChange() {
    await this.updateComplete;
    this.checkValidity();
    this.setTextareaDimensions();
  }
  updated(changedProperties) {
    if (changedProperties.has("resize")) {
      this.setTextareaDimensions();
    }
    super.updated(changedProperties);
    if (changedProperties.has("value")) {
      this.customStates.set("blank", !this.value);
    }
  }
  /** Sets focus on the textarea. */
  focus(options) {
    this.input.focus(options);
  }
  /** Removes focus from the textarea. */
  blur() {
    this.input.blur();
  }
  /** Selects all the text in the textarea. */
  select() {
    this.input.select();
  }
  /** Gets or sets the textarea's scroll position. */
  scrollPosition(position) {
    if (position) {
      if (typeof position.top === "number") this.input.scrollTop = position.top;
      if (typeof position.left === "number") this.input.scrollLeft = position.left;
      return void 0;
    }
    return {
      top: this.input.scrollTop,
      left: this.input.scrollTop
    };
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
      this.setTextareaDimensions();
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
    return x`
      <label part="form-control-label label" class="label" for="input" aria-hidden=${hasLabel ? "false" : "true"}>
        <slot name="label">${this.label}</slot>
      </label>

      <div part="base" class="textarea">
        <textarea
          part="textarea"
          id="input"
          class="control"
          title=${this.title}
          name=${o(this.name)}
          .value=${l(this.value)}
          ?disabled=${this.disabled}
          ?readonly=${this.readonly}
          ?required=${this.required}
          placeholder=${o(this.placeholder)}
          rows=${o(this.rows)}
          minlength=${o(this.minlength)}
          maxlength=${o(this.maxlength)}
          autocapitalize=${o(this.autocapitalize)}
          autocorrect=${o(this.autocorrect)}
          ?autofocus=${this.autofocus}
          spellcheck=${o(this.spellcheck)}
          enterkeyhint=${o(this.enterkeyhint)}
          inputmode=${o(this.inputmode)}
          aria-describedby="hint"
          @change=${this.handleChange}
          @input=${this.handleInput}
          @blur=${this.handleBlur}
        ></textarea>

        <!-- This "adjuster" exists to prevent layout shifting. https://github.com/shoelace-style/shoelace/issues/2180 -->
        <div part="textarea-adjuster" class="size-adjuster" ?hidden=${this.resize !== "auto"}></div>
      </div>

      <slot
        id="hint"
        name="hint"
        part="hint"
        aria-hidden=${hasHint ? "false" : "true"}
        class=${e2({
      "has-slotted": hasHint
    })}
        >${this.hint}</slot
      >
    `;
  }
};
WaTextarea.css = [textarea_default, form_control_default, size_default];
__decorateClass([
  e(".control")
], WaTextarea.prototype, "input", 2);
__decorateClass([
  e('[part~="base"]')
], WaTextarea.prototype, "base", 2);
__decorateClass([
  e(".size-adjuster")
], WaTextarea.prototype, "sizeAdjuster", 2);
__decorateClass([
  n()
], WaTextarea.prototype, "title", 2);
__decorateClass([
  n({ reflect: true })
], WaTextarea.prototype, "name", 2);
__decorateClass([
  r()
], WaTextarea.prototype, "value", 1);
__decorateClass([
  n({ attribute: "value", reflect: true })
], WaTextarea.prototype, "defaultValue", 2);
__decorateClass([
  n({ reflect: true })
], WaTextarea.prototype, "size", 2);
__decorateClass([
  n({ reflect: true })
], WaTextarea.prototype, "appearance", 2);
__decorateClass([
  n()
], WaTextarea.prototype, "label", 2);
__decorateClass([
  n({ attribute: "hint" })
], WaTextarea.prototype, "hint", 2);
__decorateClass([
  n()
], WaTextarea.prototype, "placeholder", 2);
__decorateClass([
  n({ type: Number })
], WaTextarea.prototype, "rows", 2);
__decorateClass([
  n({ reflect: true })
], WaTextarea.prototype, "resize", 2);
__decorateClass([
  n({ type: Boolean })
], WaTextarea.prototype, "disabled", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTextarea.prototype, "readonly", 2);
__decorateClass([
  n({ reflect: true })
], WaTextarea.prototype, "form", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTextarea.prototype, "required", 2);
__decorateClass([
  n({ type: Number })
], WaTextarea.prototype, "minlength", 2);
__decorateClass([
  n({ type: Number })
], WaTextarea.prototype, "maxlength", 2);
__decorateClass([
  n()
], WaTextarea.prototype, "autocapitalize", 2);
__decorateClass([
  n()
], WaTextarea.prototype, "autocorrect", 2);
__decorateClass([
  n()
], WaTextarea.prototype, "autocomplete", 2);
__decorateClass([
  n({ type: Boolean })
], WaTextarea.prototype, "autofocus", 2);
__decorateClass([
  n()
], WaTextarea.prototype, "enterkeyhint", 2);
__decorateClass([
  n({
    type: Boolean,
    converter: {
      // Allow "true|false" attribute values but keep the property boolean
      fromAttribute: (value) => !value || value === "false" ? false : true,
      toAttribute: (value) => value ? "true" : "false"
    }
  })
], WaTextarea.prototype, "spellcheck", 2);
__decorateClass([
  n()
], WaTextarea.prototype, "inputmode", 2);
__decorateClass([
  n({ attribute: "with-label", type: Boolean })
], WaTextarea.prototype, "withLabel", 2);
__decorateClass([
  n({ attribute: "with-hint", type: Boolean })
], WaTextarea.prototype, "withHint", 2);
__decorateClass([
  watch("rows", { waitUntilFirstUpdate: true })
], WaTextarea.prototype, "handleRowsChange", 1);
__decorateClass([
  watch("value", { waitUntilFirstUpdate: true })
], WaTextarea.prototype, "handleValueChange", 1);
WaTextarea = __decorateClass([
  t("wa-textarea")
], WaTextarea);

export {
  WaTextarea
};
