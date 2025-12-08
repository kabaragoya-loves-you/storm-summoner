/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  o as o2
} from "./chunk.PQKSMT7C.js";
import {
  scrollIntoView
} from "./chunk.GB2I2DTW.js";
import {
  WaClearEvent
} from "./chunk.U5TEITCX.js";
import {
  RequiredValidator
} from "./chunk.EHXOY5WV.js";
import {
  form_control_default
} from "./chunk.E2QYHS4N.js";
import {
  WaAfterHideEvent,
  WaAfterShowEvent,
  WaHideEvent,
  WaShowEvent
} from "./chunk.DTCJCEYF.js";
import {
  WebAwesomeFormAssociatedElement
} from "./chunk.D4WZJ7B2.js";
import {
  HasSlotController
} from "./chunk.SFADIYDM.js";
import {
  size_default
} from "./chunk.GY3LNU3J.js";
import {
  waitForEvent
} from "./chunk.MDCY6RAP.js";
import {
  animateWithClass
} from "./chunk.EFWMJOG7.js";
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

// src/components/select/select.css
var select_default = ":host {\n  --tag-max-size: 10ch;\n  --show-duration: 100ms;\n  --hide-duration: 100ms;\n}\n\n/* Add ellipses to multi select options */\n:host wa-tag::part(content) {\n  display: initial;\n  white-space: nowrap;\n  text-overflow: ellipsis;\n  overflow: hidden;\n  max-width: var(--tag-max-size);\n}\n\n:host .disabled [part~='combobox'] {\n  opacity: 0.5;\n  cursor: not-allowed;\n  outline: none;\n}\n\n:host .enabled:is(.open, :focus-within) [part~='combobox'] {\n  outline: var(--wa-focus-ring);\n  outline-offset: var(--wa-focus-ring-offset);\n}\n\n/** The popup */\n.select {\n  flex: 1 1 auto;\n  display: inline-flex;\n  width: 100%;\n  position: relative;\n  vertical-align: middle;\n\n  /* Pass through from select to the popup */\n  --show-duration: inherit;\n  --hide-duration: inherit;\n\n  &::part(popup) {\n    z-index: 900;\n  }\n\n  &[data-current-placement^='top']::part(popup) {\n    transform-origin: bottom;\n  }\n\n  &[data-current-placement^='bottom']::part(popup) {\n    transform-origin: top;\n  }\n}\n\n/* Combobox */\n.combobox {\n  flex: 1;\n  display: flex;\n  width: 100%;\n  min-width: 0;\n  align-items: center;\n  justify-content: start;\n\n  min-height: var(--wa-form-control-height);\n\n  background-color: var(--wa-form-control-background-color);\n  border-color: var(--wa-form-control-border-color);\n  border-radius: var(--wa-form-control-border-radius);\n  border-style: var(--wa-form-control-border-style);\n  border-width: var(--wa-form-control-border-width);\n  color: var(--wa-form-control-value-color);\n  cursor: pointer;\n  font-family: inherit;\n  font-weight: var(--wa-form-control-value-font-weight);\n  line-height: var(--wa-form-control-value-line-height);\n  overflow: hidden;\n  padding: 0 var(--wa-form-control-padding-inline);\n  position: relative;\n  vertical-align: middle;\n  width: 100%;\n  transition:\n    background-color var(--wa-transition-normal),\n    border var(--wa-transition-normal),\n    outline var(--wa-transition-fast);\n  transition-timing-function: var(--wa-transition-easing);\n\n  :host([multiple]) .select:not(.placeholder-visible) & {\n    padding-inline-start: 0;\n    padding-block: calc(var(--wa-form-control-height) * 0.1 - var(--wa-form-control-border-width));\n  }\n\n  /* Pills */\n  :host([pill]) & {\n    border-radius: var(--wa-border-radius-pill);\n  }\n}\n\n/* Appearance modifiers */\n:host([appearance='outlined']) .combobox {\n  background-color: var(--wa-form-control-background-color);\n  border-color: var(--wa-form-control-border-color);\n}\n\n:host([appearance='filled']) .combobox {\n  background-color: var(--wa-color-neutral-fill-quiet);\n  border-color: var(--wa-color-neutral-fill-quiet);\n}\n\n:host([appearance='filled-outlined']) .combobox {\n  background-color: var(--wa-color-neutral-fill-quiet);\n  border-color: var(--wa-form-control-border-color);\n}\n\n.display-input {\n  position: relative;\n  width: 100%;\n  font: inherit;\n  border: none;\n  background: none;\n  line-height: var(--wa-form-control-value-line-height);\n  color: var(--wa-form-control-value-color);\n  cursor: inherit;\n  overflow: hidden;\n  padding: 0;\n  margin: 0;\n  -webkit-appearance: none;\n\n  &:focus {\n    outline: none;\n  }\n\n  &::placeholder {\n    color: var(--wa-form-control-placeholder-color);\n  }\n}\n\n/* Visually hide the display input when multiple is enabled */\n:host([multiple]) .select:not(.placeholder-visible) .display-input {\n  position: absolute;\n  z-index: -1;\n  top: 0;\n  left: 0;\n  width: 100%;\n  height: 100%;\n  opacity: 0;\n}\n\n.value-input {\n  position: absolute;\n  z-index: -1;\n  top: 0;\n  left: 0;\n  width: 100%;\n  height: 100%;\n  opacity: 0;\n  padding: 0;\n  margin: 0;\n}\n\n.tags {\n  display: flex;\n  flex: 1;\n  align-items: center;\n  flex-wrap: wrap;\n  margin-inline-start: 0.25em;\n  gap: 0.25em;\n\n  &::slotted(wa-tag) {\n    cursor: pointer !important;\n  }\n\n  .disabled &,\n  .disabled &::slotted(wa-tag) {\n    cursor: not-allowed !important;\n  }\n}\n\n/* Start and End */\n\n.start,\n.end {\n  flex: 0;\n  display: inline-flex;\n  align-items: center;\n  color: var(--wa-color-neutral-on-quiet);\n}\n\n.end::slotted(*) {\n  margin-inline-start: var(--wa-form-control-padding-inline);\n}\n\n.start::slotted(*) {\n  margin-inline-end: var(--wa-form-control-padding-inline);\n}\n\n:host([multiple]) .start::slotted(*) {\n  margin-inline: var(--wa-form-control-padding-inline);\n}\n\n/* Clear button */\n[part~='clear-button'] {\n  display: inline-flex;\n  align-items: center;\n  justify-content: center;\n  font-size: inherit;\n  color: var(--wa-color-neutral-on-quiet);\n  border: none;\n  background: none;\n  padding: 0;\n  transition: color var(--wa-transition-normal);\n  cursor: pointer;\n  margin-inline-start: var(--wa-form-control-padding-inline);\n\n  &:focus {\n    outline: none;\n  }\n\n  @media (hover: hover) {\n    &:hover {\n      color: color-mix(in oklab, currentColor, var(--wa-color-mix-hover));\n    }\n  }\n\n  &:active {\n    color: color-mix(in oklab, currentColor, var(--wa-color-mix-active));\n  }\n}\n\n/* Expand icon */\n.expand-icon {\n  flex: 0 0 auto;\n  display: flex;\n  align-items: center;\n  color: var(--wa-color-neutral-on-quiet);\n  transition: rotate var(--wa-transition-slow) ease;\n  rotate: 0deg;\n  margin-inline-start: var(--wa-form-control-padding-inline);\n\n  .open & {\n    rotate: -180deg;\n  }\n}\n\n/* Listbox */\n.listbox {\n  display: block;\n  position: relative;\n  font: inherit;\n  box-shadow: var(--wa-shadow-m);\n  background: var(--wa-color-surface-raised);\n  border-color: var(--wa-color-surface-border);\n  border-radius: var(--wa-border-radius-m);\n  border-style: var(--wa-border-style);\n  border-width: var(--wa-border-width-s);\n  padding-block: 0.5em;\n  padding-inline: 0;\n  overflow: auto;\n  overscroll-behavior: none;\n\n  /* Make sure it adheres to the popup's auto size */\n  max-width: var(--auto-size-available-width);\n  max-height: var(--auto-size-available-height);\n\n  &::slotted(wa-divider) {\n    --spacing: 0.5em;\n  }\n}\n\nslot:not([name])::slotted(small) {\n  display: block;\n  font-size: var(--wa-font-size-smaller);\n  font-weight: var(--wa-font-weight-semibold);\n  color: var(--wa-color-text-quiet);\n  padding-block: 0.5em;\n  padding-inline: 2.25em;\n}\n";

// src/components/select/select.ts
var WaSelect = class extends WebAwesomeFormAssociatedElement {
  constructor() {
    super(...arguments);
    this.assumeInteractionOn = ["blur", "input"];
    this.hasSlotController = new HasSlotController(this, "hint", "label");
    this.localize = new LocalizeController(this);
    this.typeToSelectString = "";
    this.displayLabel = "";
    this.selectedOptions = [];
    this.name = "";
    this._defaultValue = null;
    this.size = "medium";
    this.placeholder = "";
    this.multiple = false;
    this.maxOptionsVisible = 3;
    this.disabled = false;
    this.withClear = false;
    this.open = false;
    this.appearance = "outlined";
    this.pill = false;
    this.label = "";
    this.placement = "bottom";
    this.hint = "";
    this.withLabel = false;
    this.withHint = false;
    this.form = null;
    this.required = false;
    this.getTag = (option) => {
      return x`
        <wa-tag
          part="tag"
          exportparts="
            base:tag__base,
            content:tag__content,
            remove-button:tag__remove-button,
            remove-button__base:tag__remove-button__base
          "
          ?pill=${this.pill}
          size=${this.size}
          with-remove
        >
          ${option.label}
        </wa-tag>
      `;
    };
    this.handleDocumentFocusIn = (event) => {
      const path = event.composedPath();
      if (this && !path.includes(this)) {
        this.hide();
      }
    };
    this.handleDocumentKeyDown = (event) => {
      const target = event.target;
      const isClearButton = target.closest('[part~="clear-button"]') !== null;
      const isButton = target.closest("wa-button") !== null;
      if (isClearButton || isButton) {
        return;
      }
      if (event.key === "Escape" && this.open) {
        event.preventDefault();
        event.stopPropagation();
        this.hide();
        this.displayInput.focus({ preventScroll: true });
      }
      if (event.key === "Enter" || event.key === " " && this.typeToSelectString === "") {
        event.preventDefault();
        event.stopImmediatePropagation();
        if (!this.open) {
          this.show();
          return;
        }
        if (this.currentOption && !this.currentOption.disabled) {
          this.valueHasChanged = true;
          this.hasInteracted = true;
          if (this.multiple) {
            this.toggleOptionSelection(this.currentOption);
          } else {
            this.setSelectedOptions(this.currentOption);
          }
          this.updateComplete.then(() => {
            this.dispatchEvent(new InputEvent("input", { bubbles: true, composed: true }));
            this.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
          });
          if (!this.multiple) {
            this.hide();
            this.displayInput.focus({ preventScroll: true });
          }
        }
        return;
      }
      if (["ArrowUp", "ArrowDown", "Home", "End"].includes(event.key)) {
        const allOptions = this.getAllOptions();
        const currentIndex = allOptions.indexOf(this.currentOption);
        let newIndex = Math.max(0, currentIndex);
        event.preventDefault();
        if (!this.open) {
          this.show();
          if (this.currentOption) {
            return;
          }
        }
        if (event.key === "ArrowDown") {
          newIndex = currentIndex + 1;
          if (newIndex > allOptions.length - 1) newIndex = 0;
        } else if (event.key === "ArrowUp") {
          newIndex = currentIndex - 1;
          if (newIndex < 0) newIndex = allOptions.length - 1;
        } else if (event.key === "Home") {
          newIndex = 0;
        } else if (event.key === "End") {
          newIndex = allOptions.length - 1;
        }
        this.setCurrentOption(allOptions[newIndex]);
      }
      if (event.key?.length === 1 || event.key === "Backspace") {
        const allOptions = this.getAllOptions();
        if (event.metaKey || event.ctrlKey || event.altKey) {
          return;
        }
        if (!this.open) {
          if (event.key === "Backspace") {
            return;
          }
          this.show();
        }
        event.stopPropagation();
        event.preventDefault();
        clearTimeout(this.typeToSelectTimeout);
        this.typeToSelectTimeout = window.setTimeout(() => this.typeToSelectString = "", 1e3);
        if (event.key === "Backspace") {
          this.typeToSelectString = this.typeToSelectString.slice(0, -1);
        } else {
          this.typeToSelectString += event.key.toLowerCase();
        }
        for (const option of allOptions) {
          const label = option.label.toLowerCase();
          if (label.startsWith(this.typeToSelectString)) {
            this.setCurrentOption(option);
            break;
          }
        }
      }
    };
    this.handleDocumentMouseDown = (event) => {
      const path = event.composedPath();
      if (this && !path.includes(this)) {
        this.hide();
      }
    };
  }
  static get validators() {
    const validators = o ? [] : [
      RequiredValidator({
        validationElement: Object.assign(document.createElement("select"), { required: true })
      })
    ];
    return [...super.validators, ...validators];
  }
  /** Where to anchor native constraint validation */
  get validationTarget() {
    return this.valueInput;
  }
  set defaultValue(val) {
    this._defaultValue = this.convertDefaultValue(val);
  }
  get defaultValue() {
    return this.convertDefaultValue(this._defaultValue);
  }
  /**
   * @private
   * A converter for defaultValue from array to string if its multiple. Also fixes some hydration issues.
   */
  convertDefaultValue(val) {
    const isMultiple = this.multiple || this.hasAttribute("multiple");
    if (!isMultiple && Array.isArray(val)) {
      val = val[0];
    }
    return val;
  }
  set value(val) {
    let oldValue = this.value;
    if (val instanceof FormData) {
      val = val.getAll(this.name);
    }
    if (val != null && !Array.isArray(val)) {
      val = [val];
    }
    this._value = val ?? null;
    let newValue = this.value;
    if (newValue !== oldValue) {
      this.valueHasChanged = true;
      this.requestUpdate("value", oldValue);
    }
  }
  get value() {
    let value = this._value ?? this.defaultValue ?? null;
    if (value != null) {
      value = Array.isArray(value) ? value : [value];
    }
    if (value == null) {
      this.optionValues = /* @__PURE__ */ new Set(null);
    } else {
      this.optionValues = new Set(
        this.getAllOptions().filter((option) => !option.disabled).map((option) => option.value)
      );
    }
    let ret = value;
    if (value != null) {
      ret = value.filter((v) => this.optionValues.has(v));
      ret = this.multiple ? ret : ret[0];
      ret = ret ?? null;
    }
    return ret;
  }
  connectedCallback() {
    super.connectedCallback();
    this.handleDefaultSlotChange();
    this.open = false;
  }
  updateDefaultValue() {
    const allOptions = this.getAllOptions();
    const defaultSelectedOptions = allOptions.filter((el) => el.hasAttribute("selected") || el.defaultSelected);
    if (defaultSelectedOptions.length > 0) {
      const selectedValues = defaultSelectedOptions.map((el) => el.value);
      this._defaultValue = this.multiple ? selectedValues : selectedValues[0];
    }
    if (this.hasAttribute("value")) {
      this._defaultValue = this.getAttribute("value") || null;
    }
  }
  addOpenListeners() {
    document.addEventListener("focusin", this.handleDocumentFocusIn);
    document.addEventListener("keydown", this.handleDocumentKeyDown);
    document.addEventListener("mousedown", this.handleDocumentMouseDown);
    if (this.getRootNode() !== document) {
      this.getRootNode().addEventListener("focusin", this.handleDocumentFocusIn);
    }
  }
  removeOpenListeners() {
    document.removeEventListener("focusin", this.handleDocumentFocusIn);
    document.removeEventListener("keydown", this.handleDocumentKeyDown);
    document.removeEventListener("mousedown", this.handleDocumentMouseDown);
    if (this.getRootNode() !== document) {
      this.getRootNode().removeEventListener("focusin", this.handleDocumentFocusIn);
    }
  }
  handleFocus() {
    this.displayInput.setSelectionRange(0, 0);
  }
  handleLabelClick() {
    this.displayInput.focus();
  }
  handleComboboxClick(event) {
    event.preventDefault();
  }
  handleComboboxMouseDown(event) {
    const path = event.composedPath();
    const isButton = path.some((el) => el instanceof Element && el.tagName.toLowerCase() === "wa-button");
    if (this.disabled || isButton) {
      return;
    }
    event.preventDefault();
    this.displayInput.focus({ preventScroll: true });
    this.open = !this.open;
  }
  handleComboboxKeyDown(event) {
    event.stopPropagation();
    this.handleDocumentKeyDown(event);
  }
  handleClearClick(event) {
    event.stopPropagation();
    if (this.value !== null) {
      this.setSelectedOptions([]);
      this.displayInput.focus({ preventScroll: true });
      this.updateComplete.then(() => {
        this.dispatchEvent(new WaClearEvent());
        this.dispatchEvent(new InputEvent("input", { bubbles: true, composed: true }));
        this.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
      });
    }
  }
  handleClearMouseDown(event) {
    event.stopPropagation();
    event.preventDefault();
  }
  handleOptionClick(event) {
    const target = event.target;
    const option = target.closest("wa-option");
    if (option && !option.disabled) {
      this.hasInteracted = true;
      this.valueHasChanged = true;
      if (this.multiple) {
        this.toggleOptionSelection(option);
      } else {
        this.setSelectedOptions(option);
      }
      this.updateComplete.then(() => this.displayInput.focus({ preventScroll: true }));
      this.requestUpdate("value");
      this.updateComplete.then(() => {
        this.dispatchEvent(new InputEvent("input", { bubbles: true, composed: true }));
        this.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
      });
      if (!this.multiple) {
        this.hide();
        this.displayInput.focus({ preventScroll: true });
      }
    }
  }
  /* @internal - used by options to update labels */
  handleDefaultSlotChange() {
    if (!customElements.get("wa-option")) {
      customElements.whenDefined("wa-option").then(() => this.handleDefaultSlotChange());
    }
    const allOptions = this.getAllOptions();
    this.optionValues = void 0;
    this.updateDefaultValue();
    let value = this.value;
    if (value == null || !this.valueHasChanged && !this.hasInteracted) {
      this.selectionChanged();
      return;
    }
    if (!Array.isArray(value)) {
      value = [value];
    }
    const selectedOptions = allOptions.filter((el) => value.includes(el.value));
    this.setSelectedOptions(selectedOptions);
  }
  handleTagRemove(event, directOption) {
    event.stopPropagation();
    if (this.disabled) return;
    let option = directOption;
    if (!option) {
      const tagElement = event.target.closest("wa-tag[part~=tag]");
      if (tagElement) {
        const tagsContainer = this.shadowRoot?.querySelector('[part="tags"]');
        if (tagsContainer) {
          const allTags = Array.from(tagsContainer.children);
          const index = allTags.indexOf(tagElement);
          if (index >= 0 && index < this.selectedOptions.length) {
            option = this.selectedOptions[index];
          }
        }
      }
    }
    if (option) {
      this.toggleOptionSelection(option, false);
      this.updateComplete.then(() => {
        this.dispatchEvent(new InputEvent("input", { bubbles: true, composed: true }));
        this.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
      });
    }
  }
  // Gets an array of all `<wa-option>` elements
  getAllOptions() {
    if (!this?.querySelectorAll) {
      return [];
    }
    return [...this.querySelectorAll("wa-option")];
  }
  // Gets the first `<wa-option>` element
  getFirstOption() {
    return this.querySelector("wa-option");
  }
  // Sets the current option, which is the option the user is currently interacting with (e.g. via keyboard). Only one
  // option may be "current" at a time.
  setCurrentOption(option) {
    const allOptions = this.getAllOptions();
    allOptions.forEach((el) => {
      el.current = false;
      el.tabIndex = -1;
    });
    if (option) {
      this.currentOption = option;
      option.current = true;
      option.tabIndex = 0;
      option.focus();
    }
  }
  // Sets the selected option(s)
  setSelectedOptions(option) {
    const allOptions = this.getAllOptions();
    const newSelectedOptions = Array.isArray(option) ? option : [option];
    allOptions.forEach((el) => {
      if (newSelectedOptions.includes(el)) {
        return;
      }
      el.selected = false;
    });
    if (newSelectedOptions.length) {
      newSelectedOptions.forEach((el) => el.selected = true);
    }
    this.selectionChanged();
  }
  // Toggles an option's selected state
  toggleOptionSelection(option, force) {
    if (force === true || force === false) {
      option.selected = force;
    } else {
      option.selected = !option.selected;
    }
    this.selectionChanged();
  }
  // @internal This method must be called whenever the selection changes. It will update the selected options cache, the
  // current value, and the display value. The option component uses it internally to update labels as they change.
  selectionChanged() {
    const options = this.getAllOptions();
    this.selectedOptions = options.filter((el) => {
      if (!this.hasInteracted && !this.valueHasChanged) {
        const defaultValue = this.defaultValue;
        const defaultValues = Array.isArray(defaultValue) ? defaultValue : [defaultValue];
        return el.hasAttribute("selected") || el.defaultSelected || el.selected || defaultValues?.includes(el.value);
      }
      return el.selected;
    });
    let selectedValues = new Set(this.selectedOptions.map((el) => el.value));
    if (selectedValues.size > 0 || this._value) {
      const oldValue = this._value;
      if (this._value == null) {
        let value = this.defaultValue ?? [];
        this._value = Array.isArray(value) ? value : [value];
      }
      this._value = this._value?.filter((value) => !this.optionValues?.has(value)) ?? null;
      this._value?.unshift(...selectedValues);
      this.requestUpdate("value", oldValue);
    }
    if (this.multiple) {
      if (this.placeholder && !this.value?.length) {
        this.displayLabel = "";
      } else {
        this.displayLabel = this.localize.term("numOptionsSelected", this.selectedOptions.length);
      }
    } else {
      const selectedOption = this.selectedOptions[0];
      this.displayLabel = selectedOption?.label ?? "";
    }
    this.updateComplete.then(() => {
      this.updateValidity();
    });
  }
  get tags() {
    return this.selectedOptions.map((option, index) => {
      if (index < this.maxOptionsVisible || this.maxOptionsVisible <= 0) {
        const tag = this.getTag(option, index);
        if (!tag) return null;
        return typeof tag === "string" ? o2(tag) : tag;
      } else if (index === this.maxOptionsVisible) {
        return x`
          <wa-tag
            part="tag"
            exportparts="
              base:tag__base,
              content:tag__content,
              remove-button:tag__remove-button,
              remove-button__base:tag__remove-button__base
            "
            >+${this.selectedOptions.length - index}</wa-tag
          >
        `;
      }
      return null;
    });
  }
  updated(changedProperties) {
    super.updated(changedProperties);
    if (changedProperties.has("value")) {
      this.customStates.set("blank", !this.value);
    }
  }
  handleDisabledChange() {
    if (this.disabled && this.open) {
      this.open = false;
    }
  }
  handleValueChange() {
    const allOptions = this.getAllOptions();
    const value = Array.isArray(this.value) ? this.value : [this.value];
    const selectedOptions = allOptions.filter((el) => value.includes(el.value));
    this.setSelectedOptions(selectedOptions);
    this.updateValidity();
  }
  async handleOpenChange() {
    if (this.open && !this.disabled) {
      this.setCurrentOption(this.selectedOptions[0] || this.getFirstOption());
      const waShowEvent = new WaShowEvent();
      this.dispatchEvent(waShowEvent);
      if (waShowEvent.defaultPrevented) {
        this.open = false;
        return;
      }
      this.addOpenListeners();
      this.listbox.hidden = false;
      this.popup.active = true;
      requestAnimationFrame(() => {
        this.setCurrentOption(this.currentOption);
      });
      await animateWithClass(this.popup.popup, "show");
      if (this.currentOption) {
        scrollIntoView(this.currentOption, this.listbox, "vertical", "auto");
      }
      this.dispatchEvent(new WaAfterShowEvent());
    } else {
      const waHideEvent = new WaHideEvent();
      this.dispatchEvent(waHideEvent);
      if (waHideEvent.defaultPrevented) {
        this.open = false;
        return;
      }
      this.removeOpenListeners();
      await animateWithClass(this.popup.popup, "hide");
      this.listbox.hidden = true;
      this.popup.active = false;
      this.dispatchEvent(new WaAfterHideEvent());
    }
  }
  /** Shows the listbox. */
  async show() {
    if (this.open || this.disabled) {
      this.open = false;
      return void 0;
    }
    this.open = true;
    return waitForEvent(this, "wa-after-show");
  }
  /** Hides the listbox. */
  async hide() {
    if (!this.open || this.disabled) {
      this.open = false;
      return void 0;
    }
    this.open = false;
    return waitForEvent(this, "wa-after-hide");
  }
  /** Sets focus on the control. */
  focus(options) {
    this.displayInput.focus(options);
  }
  /** Removes focus from the control. */
  blur() {
    this.displayInput.blur();
  }
  formResetCallback() {
    this.value = this.defaultValue;
    super.formResetCallback();
    this.handleValueChange();
    this.updateComplete.then(() => {
      this.dispatchEvent(new InputEvent("input", { bubbles: true, composed: true }));
      this.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
    });
  }
  render() {
    const hasLabelSlot = this.hasUpdated ? this.hasSlotController.test("label") : this.withLabel;
    const hasHintSlot = this.hasUpdated ? this.hasSlotController.test("hint") : this.withHint;
    const hasLabel = this.label ? true : !!hasLabelSlot;
    const hasHint = this.hint ? true : !!hasHintSlot;
    const hasClearIcon = (this.hasUpdated || o) && this.withClear && !this.disabled && this.value && this.value.length > 0;
    const isPlaceholderVisible = Boolean(this.placeholder && (!this.value || this.value.length === 0));
    return x`
      <div
        part="form-control"
        class=${e2({
      "form-control": true,
      "form-control-has-label": hasLabel
    })}
      >
        <label
          id="label"
          part="form-control-label label"
          class="label"
          aria-hidden=${hasLabel ? "false" : "true"}
          @click=${this.handleLabelClick}
        >
          <slot name="label">${this.label}</slot>
        </label>

        <div part="form-control-input" class="form-control-input">
          <wa-popup
            class=${e2({
      select: true,
      open: this.open,
      disabled: this.disabled,
      enabled: !this.disabled,
      multiple: this.multiple,
      "placeholder-visible": isPlaceholderVisible
    })}
            placement=${this.placement}
            flip
            shift
            sync="width"
            auto-size="vertical"
            auto-size-padding="10"
          >
            <div
              part="combobox"
              class="combobox"
              slot="anchor"
              @keydown=${this.handleComboboxKeyDown}
              @mousedown=${this.handleComboboxMouseDown}
              @click=${this.handleComboboxClick}
            >
              <slot part="start" name="start" class="start"></slot>

              <input
                part="display-input"
                class="display-input"
                type="text"
                placeholder=${this.placeholder}
                .disabled=${this.disabled}
                .value=${this.displayLabel}
                ?required=${this.required}
                autocomplete="off"
                spellcheck="false"
                autocapitalize="off"
                readonly
                aria-invalid=${!this.validity.valid}
                aria-controls="listbox"
                aria-expanded=${this.open ? "true" : "false"}
                aria-haspopup="listbox"
                aria-labelledby="label"
                aria-disabled=${this.disabled ? "true" : "false"}
                aria-describedby="hint"
                role="combobox"
                tabindex="0"
                @focus=${this.handleFocus}
              />

              <!-- Tags need to wait for first hydration before populating otherwise it will create a hydration mismatch. -->
              ${this.multiple && this.hasUpdated ? x`<div part="tags" class="tags" @wa-remove=${this.handleTagRemove}>${this.tags}</div>` : ""}

              <input
                class="value-input"
                type="text"
                ?disabled=${this.disabled}
                ?required=${this.required}
                .value=${Array.isArray(this.value) ? this.value.join(", ") : this.value}
                tabindex="-1"
                aria-hidden="true"
                @focus=${() => this.focus()}
              />

              ${hasClearIcon ? x`
                    <button
                      part="clear-button"
                      type="button"
                      aria-label=${this.localize.term("clearEntry")}
                      @mousedown=${this.handleClearMouseDown}
                      @click=${this.handleClearClick}
                      tabindex="-1"
                    >
                      <slot name="clear-icon">
                        <wa-icon name="circle-xmark" library="system" variant="regular"></wa-icon>
                      </slot>
                    </button>
                  ` : ""}

              <slot name="end" part="end" class="end"></slot>

              <slot name="expand-icon" part="expand-icon" class="expand-icon">
                <wa-icon library="system" name="chevron-down" variant="solid"></wa-icon>
              </slot>
            </div>

            <div
              id="listbox"
              role="listbox"
              aria-expanded=${this.open ? "true" : "false"}
              aria-multiselectable=${this.multiple ? "true" : "false"}
              aria-labelledby="label"
              part="listbox"
              class="listbox"
              tabindex="-1"
              @mouseup=${this.handleOptionClick}
            >
              <slot @slotchange=${this.handleDefaultSlotChange}></slot>
            </div>
          </wa-popup>
        </div>

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
      </div>
    `;
  }
};
WaSelect.css = [select_default, form_control_default, size_default];
__decorateClass([
  e(".select")
], WaSelect.prototype, "popup", 2);
__decorateClass([
  e(".combobox")
], WaSelect.prototype, "combobox", 2);
__decorateClass([
  e(".display-input")
], WaSelect.prototype, "displayInput", 2);
__decorateClass([
  e(".value-input")
], WaSelect.prototype, "valueInput", 2);
__decorateClass([
  e(".listbox")
], WaSelect.prototype, "listbox", 2);
__decorateClass([
  r()
], WaSelect.prototype, "displayLabel", 2);
__decorateClass([
  r()
], WaSelect.prototype, "currentOption", 2);
__decorateClass([
  r()
], WaSelect.prototype, "selectedOptions", 2);
__decorateClass([
  r()
], WaSelect.prototype, "optionValues", 2);
__decorateClass([
  n()
], WaSelect.prototype, "name", 2);
__decorateClass([
  n({
    attribute: false
  })
], WaSelect.prototype, "defaultValue", 1);
__decorateClass([
  n({ attribute: "value", reflect: false })
], WaSelect.prototype, "value", 1);
__decorateClass([
  n({ reflect: true })
], WaSelect.prototype, "size", 2);
__decorateClass([
  n()
], WaSelect.prototype, "placeholder", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaSelect.prototype, "multiple", 2);
__decorateClass([
  n({ attribute: "max-options-visible", type: Number })
], WaSelect.prototype, "maxOptionsVisible", 2);
__decorateClass([
  n({ type: Boolean })
], WaSelect.prototype, "disabled", 2);
__decorateClass([
  n({ attribute: "with-clear", type: Boolean })
], WaSelect.prototype, "withClear", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaSelect.prototype, "open", 2);
__decorateClass([
  n({ reflect: true })
], WaSelect.prototype, "appearance", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaSelect.prototype, "pill", 2);
__decorateClass([
  n()
], WaSelect.prototype, "label", 2);
__decorateClass([
  n({ reflect: true })
], WaSelect.prototype, "placement", 2);
__decorateClass([
  n({ attribute: "hint" })
], WaSelect.prototype, "hint", 2);
__decorateClass([
  n({ attribute: "with-label", type: Boolean })
], WaSelect.prototype, "withLabel", 2);
__decorateClass([
  n({ attribute: "with-hint", type: Boolean })
], WaSelect.prototype, "withHint", 2);
__decorateClass([
  n({ reflect: true })
], WaSelect.prototype, "form", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaSelect.prototype, "required", 2);
__decorateClass([
  n({ attribute: false })
], WaSelect.prototype, "getTag", 2);
__decorateClass([
  watch("disabled", { waitUntilFirstUpdate: true })
], WaSelect.prototype, "handleDisabledChange", 1);
__decorateClass([
  watch("value", { waitUntilFirstUpdate: true })
], WaSelect.prototype, "handleValueChange", 1);
__decorateClass([
  watch("open", { waitUntilFirstUpdate: true })
], WaSelect.prototype, "handleOpenChange", 1);
WaSelect = __decorateClass([
  t("wa-select")
], WaSelect);

export {
  WaSelect
};
