/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  o
} from "./chunk.G3G3UFWM.js";
import {
  watch
} from "./chunk.N3AZYXKV.js";
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

// src/components/breadcrumb-item/breadcrumb-item.css
var breadcrumb_item_default = ":host {\n  color: var(--wa-color-text-link);\n  display: inline-flex;\n  align-items: center;\n  font: inherit;\n  font-weight: var(--wa-font-weight-action);\n  line-height: var(--wa-line-height-normal);\n  white-space: nowrap;\n}\n\n:host(:last-of-type) {\n  color: var(--wa-color-text-quiet);\n}\n\n.label {\n  display: inline-block;\n  font: inherit;\n  text-decoration: none;\n  color: currentColor;\n  background: none;\n  border: none;\n  border-radius: var(--wa-border-radius-m);\n  padding: 0;\n  margin: 0;\n  cursor: pointer;\n  transition: color var(--wa-transition-normal) var(--wa-transition-easing);\n}\n\n@media (hover: hover) {\n  :host(:not(:last-of-type)) .label:hover {\n    color: color-mix(in oklab, currentColor, var(--wa-color-mix-hover));\n  }\n}\n\n:host(:not(:last-of-type)) .label:active {\n  color: color-mix(in oklab, currentColor, var(--wa-color-mix-active));\n}\n\n.label:focus {\n  outline: none;\n}\n\n.label:focus-visible {\n  outline: var(--wa-focus-ring);\n  outline-offset: var(--wa-focus-ring-offset);\n}\n\n.start,\n.end {\n  display: none;\n  flex: 0 0 auto;\n  display: flex;\n  align-items: center;\n}\n\n.start,\n.end {\n  display: inline-flex;\n  color: var(--wa-color-text-quiet);\n}\n\n::slotted([slot='start']) {\n  margin-inline-end: var(--wa-space-s);\n}\n\n::slotted([slot='end']) {\n  margin-inline-start: var(--wa-space-s);\n}\n\n:host(:last-of-type) .separator {\n  display: none;\n}\n\n.separator {\n  color: var(--wa-color-text-quiet);\n  display: inline-flex;\n  align-items: center;\n  margin: 0 var(--wa-space-s);\n  user-select: none;\n  -webkit-user-select: none;\n}\n";

// src/components/breadcrumb-item/breadcrumb-item.ts
var WaBreadcrumbItem = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.renderType = "button";
    this.rel = "noreferrer noopener";
  }
  setRenderType() {
    const hasDropdown = this.defaultSlot.assignedElements({ flatten: true }).filter((i) => i.tagName.toLowerCase() === "wa-dropdown").length > 0;
    if (this.href) {
      this.renderType = "link";
      return;
    }
    if (hasDropdown) {
      this.renderType = "dropdown";
      return;
    }
    this.renderType = "button";
  }
  hrefChanged() {
    this.setRenderType();
  }
  handleSlotChange() {
    this.setRenderType();
  }
  render() {
    return x`
      <span part="start" class="start">
        <slot name="start"></slot>
      </span>

      ${this.renderType === "link" ? x`
            <a
              part="label"
              class="label label-link"
              href="${this.href}"
              target="${o(this.target ? this.target : void 0)}"
              rel=${o(this.target ? this.rel : void 0)}
            >
              <slot></slot>
            </a>
          ` : ""}
      ${this.renderType === "button" ? x`
            <button part="label" type="button" class="label label-button">
              <slot @slotchange=${this.handleSlotChange}></slot>
            </button>
          ` : ""}
      ${this.renderType === "dropdown" ? x`
            <div part="label" class="label label-dropdown">
              <slot @slotchange=${this.handleSlotChange}></slot>
            </div>
          ` : ""}

      <span part="end" class="end">
        <slot name="end"></slot>
      </span>

      <span part="separator" class="separator" aria-hidden="true">
        <slot name="separator"></slot>
      </span>
    `;
  }
};
WaBreadcrumbItem.css = breadcrumb_item_default;
__decorateClass([
  e("slot:not([name])")
], WaBreadcrumbItem.prototype, "defaultSlot", 2);
__decorateClass([
  r()
], WaBreadcrumbItem.prototype, "renderType", 2);
__decorateClass([
  n()
], WaBreadcrumbItem.prototype, "href", 2);
__decorateClass([
  n()
], WaBreadcrumbItem.prototype, "target", 2);
__decorateClass([
  n()
], WaBreadcrumbItem.prototype, "rel", 2);
__decorateClass([
  watch("href", { waitUntilFirstUpdate: true })
], WaBreadcrumbItem.prototype, "hrefChanged", 1);
WaBreadcrumbItem = __decorateClass([
  t("wa-breadcrumb-item")
], WaBreadcrumbItem);

export {
  WaBreadcrumbItem
};
