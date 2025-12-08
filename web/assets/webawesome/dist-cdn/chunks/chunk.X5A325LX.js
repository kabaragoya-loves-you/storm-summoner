/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  LocalizeController
} from "./chunk.DR3YY3XN.js";
import {
  WebAwesomeElement,
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

// src/internal/get-text.ts
function getText(root, depth = 0) {
  if (!root || !globalThis.Node) {
    return "";
  }
  if (typeof root[Symbol.iterator] === "function") {
    let nodes = Array.isArray(root) ? root : [...root];
    return nodes.map((node2) => getText(node2, --depth)).join("");
  }
  let node = root;
  if (node.nodeType === Node.TEXT_NODE) {
    return node.textContent ?? "";
  }
  if (node.nodeType === Node.ELEMENT_NODE) {
    let element = node;
    if (element.hasAttribute("slot") || element.matches("style, script")) {
      return "";
    }
    if (element instanceof HTMLSlotElement) {
      let assignedNodes = element.assignedNodes({ flatten: true });
      if (assignedNodes.length > 0) {
        return getText(assignedNodes, --depth);
      }
    }
    return depth > -1 ? getText(element, --depth) : element.textContent ?? "";
  }
  return node.hasChildNodes() ? getText(node.childNodes, --depth) : "";
}

// src/components/option/option.css
var option_default = ":host {\n  display: block;\n  color: var(--wa-color-text-normal);\n  -webkit-user-select: none;\n  user-select: none;\n\n  position: relative;\n  display: flex;\n  align-items: center;\n  font: inherit;\n  padding: 0.5em 1em 0.5em 0.25em;\n  line-height: var(--wa-line-height-condensed);\n  transition: fill var(--wa-transition-normal) var(--wa-transition-easing);\n  cursor: pointer;\n}\n\n:host(:focus) {\n  outline: none;\n}\n\n@media (hover: hover) {\n  :host(:not([disabled], :state(current)):is(:state(hover), :hover)) {\n    background-color: var(--wa-color-neutral-fill-normal);\n    color: var(--wa-color-neutral-on-normal);\n  }\n}\n\n:host(:state(current)),\n:host([disabled]:state(current)) {\n  background-color: var(--wa-color-brand-fill-loud);\n  color: var(--wa-color-brand-on-loud);\n  opacity: 1;\n}\n\n:host([disabled]) {\n  outline: none;\n  opacity: 0.5;\n  cursor: not-allowed;\n}\n\n.label {\n  flex: 1 1 auto;\n  display: inline-block;\n}\n\n.check {\n  flex: 0 0 auto;\n  display: flex;\n  align-items: center;\n  justify-content: center;\n  font-size: var(--wa-font-size-smaller);\n  visibility: hidden;\n  width: 2em;\n}\n\n:host(:state(selected)) .check {\n  visibility: visible;\n}\n\n.start,\n.end {\n  flex: 0 0 auto;\n  display: flex;\n  align-items: center;\n}\n\n.start::slotted(*) {\n  margin-inline-end: 0.5em;\n}\n\n.end::slotted(*) {\n  margin-inline-start: 0.5em;\n}\n\n@media (forced-colors: active) {\n  :host(:hover:not([aria-disabled='true'])) {\n    outline: dashed 1px SelectedItem;\n    outline-offset: -1px;\n  }\n}\n";

// src/components/option/option.ts
var WaOption = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    // @ts-expect-error - Controller is currently unused
    this.localize = new LocalizeController(this);
    this.isInitialized = false;
    this.current = false;
    this.value = "";
    this.disabled = false;
    this.selected = false;
    this.defaultSelected = false;
    this._label = "";
    this.defaultLabel = "";
    this.handleHover = (event) => {
      if (event.type === "mouseenter") {
        this.customStates.set("hover", true);
      } else if (event.type === "mouseleave") {
        this.customStates.set("hover", false);
      }
    };
  }
  set label(value) {
    const oldValue = this._label;
    this._label = value || "";
    if (this._label !== oldValue) {
      this.requestUpdate("label", oldValue);
    }
  }
  get label() {
    if (this._label) {
      return this._label;
    }
    if (!this.defaultLabel) {
      this.updateDefaultLabel();
    }
    return this.defaultLabel;
  }
  connectedCallback() {
    super.connectedCallback();
    this.setAttribute("role", "option");
    this.setAttribute("aria-selected", "false");
    this.addEventListener("mouseenter", this.handleHover);
    this.addEventListener("mouseleave", this.handleHover);
    this.updateDefaultLabel();
  }
  disconnectedCallback() {
    super.disconnectedCallback();
    this.removeEventListener("mouseenter", this.handleHover);
    this.removeEventListener("mouseleave", this.handleHover);
  }
  handleDefaultSlotChange() {
    this.updateDefaultLabel();
    if (this.isInitialized) {
      customElements.whenDefined("wa-select").then(() => {
        const controller = this.closest("wa-select");
        if (controller) {
          controller.handleDefaultSlotChange();
          controller.selectionChanged?.();
        }
      });
    } else {
      this.isInitialized = true;
    }
  }
  willUpdate(changedProperties) {
    if (changedProperties.has("defaultSelected")) {
      if (!this.closest("wa-select")?.hasInteracted) {
        const oldVal = this.selected;
        this.selected = this.defaultSelected;
        this.requestUpdate("selected", oldVal);
      }
    }
    super.willUpdate(changedProperties);
  }
  updated(changedProperties) {
    super.updated(changedProperties);
    if (changedProperties.has("disabled")) {
      this.setAttribute("aria-disabled", this.disabled ? "true" : "false");
    }
    if (changedProperties.has("selected")) {
      this.setAttribute("aria-selected", this.selected ? "true" : "false");
      this.customStates.set("selected", this.selected);
      this.handleDefaultSlotChange();
    }
    if (changedProperties.has("value")) {
      if (typeof this.value !== "string") {
        this.value = String(this.value);
      }
      this.handleDefaultSlotChange();
    }
    if (changedProperties.has("current")) {
      this.customStates.set("current", this.current);
    }
  }
  updateDefaultLabel() {
    let oldValue = this.defaultLabel;
    this.defaultLabel = getText(this).trim();
    let changed = this.defaultLabel !== oldValue;
    if (!this._label && changed) {
      this.requestUpdate("label", oldValue);
    }
    return changed;
  }
  render() {
    return x`
      <wa-icon
        part="checked-icon"
        class="check"
        name="check"
        library="system"
        variant="solid"
        aria-hidden="true"
      ></wa-icon>
      <slot part="start" name="start" class="start"></slot>
      <slot part="label" class="label" @slotchange=${this.handleDefaultSlotChange}></slot>
      <slot part="end" name="end" class="end"></slot>
    `;
  }
};
WaOption.css = option_default;
__decorateClass([
  e(".label")
], WaOption.prototype, "defaultSlot", 2);
__decorateClass([
  r()
], WaOption.prototype, "current", 2);
__decorateClass([
  n({ reflect: true })
], WaOption.prototype, "value", 2);
__decorateClass([
  n({ type: Boolean })
], WaOption.prototype, "disabled", 2);
__decorateClass([
  n({ type: Boolean, attribute: false })
], WaOption.prototype, "selected", 2);
__decorateClass([
  n({ type: Boolean, attribute: "selected" })
], WaOption.prototype, "defaultSelected", 2);
__decorateClass([
  n()
], WaOption.prototype, "label", 1);
__decorateClass([
  r()
], WaOption.prototype, "defaultLabel", 2);
WaOption = __decorateClass([
  t("wa-option")
], WaOption);

export {
  WaOption
};
