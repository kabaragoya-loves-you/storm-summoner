/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  variants_default
} from "./chunk.R6O3GHMM.js";
import {
  e as e2
} from "./chunk.6MY2PWCO.js";
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

// src/components/button-group/button-group.css
var button_group_default = ":host {\n  display: inline-flex;\n}\n\n.button-group {\n  display: flex;\n  position: relative;\n  isolation: isolate;\n  flex-wrap: wrap;\n  gap: 1px;\n\n  @media (hover: hover) {\n    > :hover,\n    &::slotted(:hover) {\n      z-index: 1;\n    }\n  }\n\n  /* Focus and checked are always on top */\n  > :focus,\n  &::slotted(:focus),\n  > [aria-checked='true'],\n  &::slotted([aria-checked='true']),\n  > [checked],\n  &::slotted([checked]) {\n    z-index: 2 !important;\n  }\n}\n:host([orientation='vertical']) .button-group {\n  flex-direction: column;\n}\n\n/* Button groups with at least one outlined button will not have a gap and instead have borders overlap */\n.button-group.has-outlined {\n  gap: 0;\n\n  &:not([aria-orientation='vertical']):not(.button-group-vertical)::slotted(:not(:first-child)) {\n    margin-inline-start: calc(-1 * var(--border-width));\n  }\n\n  &:is([aria-orientation='vertical'], .button-group-vertical)::slotted(:not(:first-child)) {\n    margin-block-start: calc(-1 * var(--border-width));\n  }\n}\n";

// src/components/button-group/button-group.ts
var WaButtonGroup = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.disableRole = false;
    this.hasOutlined = false;
    this.label = "";
    this.orientation = "horizontal";
    this.variant = "neutral";
  }
  updated(changedProperties) {
    super.updated(changedProperties);
    if (changedProperties.has("orientation")) {
      this.setAttribute("aria-orientation", this.orientation);
      this.updateClassNames();
    }
  }
  handleFocus(event) {
    const button = findButton(event.target);
    button?.classList.add("button-focus");
  }
  handleBlur(event) {
    const button = findButton(event.target);
    button?.classList.remove("button-focus");
  }
  handleMouseOver(event) {
    const button = findButton(event.target);
    button?.classList.add("button-hover");
  }
  handleMouseOut(event) {
    const button = findButton(event.target);
    button?.classList.remove("button-hover");
  }
  handleSlotChange() {
    this.updateClassNames();
  }
  updateClassNames() {
    const slottedElements = [...this.defaultSlot.assignedElements({ flatten: true })];
    this.hasOutlined = false;
    slottedElements.forEach((el) => {
      const index = slottedElements.indexOf(el);
      const button = findButton(el);
      if (button) {
        if (button.appearance === "outlined") this.hasOutlined = true;
        button.classList.add("wa-button-group__button");
        button.classList.toggle("wa-button-group__horizontal", this.orientation === "horizontal");
        button.classList.toggle("wa-button-group__vertical", this.orientation === "vertical");
        button.classList.toggle("wa-button-group__button-first", index === 0);
        button.classList.toggle("wa-button-group__button-inner", index > 0 && index < slottedElements.length - 1);
        button.classList.toggle("wa-button-group__button-last", index === slottedElements.length - 1);
        button.classList.toggle("wa-button-group__button-radio", button.tagName.toLowerCase() === "wa-radio-button");
      }
    });
  }
  render() {
    return x`
      <slot
        part="base"
        class=${e2({
      "button-group": true,
      "has-outlined": this.hasOutlined
    })}
        role="${this.disableRole ? "presentation" : "group"}"
        aria-label=${this.label}
        aria-orientation=${this.orientation}
        @focusout=${this.handleBlur}
        @focusin=${this.handleFocus}
        @mouseover=${this.handleMouseOver}
        @mouseout=${this.handleMouseOut}
        @slotchange=${this.handleSlotChange}
      ></slot>
    `;
  }
};
WaButtonGroup.css = [variants_default, button_group_default];
__decorateClass([
  e("slot")
], WaButtonGroup.prototype, "defaultSlot", 2);
__decorateClass([
  r()
], WaButtonGroup.prototype, "disableRole", 2);
__decorateClass([
  r()
], WaButtonGroup.prototype, "hasOutlined", 2);
__decorateClass([
  n()
], WaButtonGroup.prototype, "label", 2);
__decorateClass([
  n({ reflect: true })
], WaButtonGroup.prototype, "orientation", 2);
__decorateClass([
  n({ reflect: true })
], WaButtonGroup.prototype, "variant", 2);
WaButtonGroup = __decorateClass([
  t("wa-button-group")
], WaButtonGroup);
function findButton(el) {
  const selector = "wa-button, wa-radio-button";
  return el.closest(selector) ?? el.querySelector(selector);
}

export {
  WaButtonGroup
};
