/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  l
} from "./chunk.T7SKOEAS.js";
import {
  animate,
  parseDuration
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

// ../../node_modules/lit-html/directives/when.js
function n2(n3, r2, t2) {
  return n3 ? r2(n3) : t2?.(n3);
}

// src/events/after-collapse.ts
var WaAfterCollapseEvent = class extends Event {
  constructor() {
    super("wa-after-collapse", { bubbles: true, cancelable: false, composed: true });
  }
};

// src/events/after-expand.ts
var WaAfterExpandEvent = class extends Event {
  constructor() {
    super("wa-after-expand", { bubbles: true, cancelable: false, composed: true });
  }
};

// src/events/collapse.ts
var WaCollapseEvent = class extends Event {
  constructor() {
    super("wa-collapse", { bubbles: true, cancelable: false, composed: true });
  }
};

// src/events/expand.ts
var WaExpandEvent = class extends Event {
  constructor() {
    super("wa-expand", { bubbles: true, cancelable: false, composed: true });
  }
};

// src/events/lazy-change.ts
var WaLazyChangeEvent = class extends Event {
  constructor() {
    super("wa-lazy-change", { bubbles: true, cancelable: false, composed: true });
  }
};

// src/events/lazy-load.ts
var WaLazyLoadEvent = class extends Event {
  constructor() {
    super("wa-lazy-load", { bubbles: true, cancelable: false, composed: true });
  }
};

// src/components/tree-item/tree-item.css
var tree_item_default = ":host {\n  --show-duration: 200ms;\n  --hide-duration: 200ms;\n\n  display: block;\n  color: var(--wa-color-text-normal);\n  outline: 0;\n  z-index: 0;\n}\n\n:host(:focus) {\n  outline: none;\n}\n\nslot:not([name])::slotted(wa-icon) {\n  margin-inline-end: var(--wa-space-xs);\n}\n\n.tree-item {\n  position: relative;\n  display: flex;\n  align-items: stretch;\n  flex-direction: column;\n  cursor: default;\n  user-select: none;\n  -webkit-user-select: none;\n}\n\n.checkbox {\n  line-height: var(--wa-form-control-value-line-height);\n  pointer-events: none;\n}\n\n.expand-button,\n.checkbox,\n.label {\n  font-family: inherit;\n  font-size: var(--wa-font-size-m);\n  font-weight: inherit;\n}\n\n.checkbox::part(base) {\n  display: flex;\n  align-items: center;\n}\n\n.indentation {\n  display: block;\n  width: 1em;\n  flex-shrink: 0;\n}\n\n.expand-button {\n  display: flex;\n  align-items: center;\n  justify-content: center;\n  color: var(--wa-color-text-quiet);\n  width: 2em;\n  height: 2em;\n  flex-shrink: 0;\n  cursor: pointer;\n}\n\n.expand-button {\n  transition: rotate var(--wa-transition-normal) var(--wa-transition-easing);\n}\n\n.tree-item-expanded .expand-button {\n  rotate: 90deg;\n}\n\n.tree-item-expanded:dir(rtl) .expand-button {\n  rotate: -90deg;\n}\n\n.tree-item-expanded slot[name='expand-icon'],\n.tree-item:not(.tree-item-expanded) slot[name='collapse-icon'] {\n  display: none;\n}\n\n.tree-item:not(.tree-item-has-expand-button) .expand-icon-slot {\n  display: none;\n}\n\n.expand-button-visible {\n  cursor: pointer;\n}\n\n.item {\n  display: flex;\n  align-items: center;\n  border-inline-start: solid 3px transparent;\n}\n\n:host([disabled]) .item {\n  opacity: 0.5;\n  outline: none;\n  cursor: not-allowed;\n}\n\n:host(:focus-visible) .item {\n  outline: var(--wa-focus-ring);\n  outline-offset: var(--wa-focus-ring-offset);\n  z-index: 2;\n}\n\n:host(:not([aria-disabled='true'])) .tree-item-selected .item {\n  background-color: var(--wa-color-neutral-fill-quiet);\n  border-inline-start-color: var(--wa-color-brand-fill-loud);\n}\n\n:host(:not([aria-disabled='true'])) .expand-button {\n  color: var(--wa-color-text-quiet);\n}\n\n.label {\n  display: flex;\n  align-items: center;\n  transition: color var(--wa-transition-normal) var(--wa-transition-easing);\n}\n\n.children {\n  display: block;\n  font-size: calc(1em + var(--indent-size, var(--wa-space-m)));\n}\n\n/* Indentation lines */\n.children {\n  position: relative;\n}\n\n.children::before {\n  content: '';\n  position: absolute;\n  top: var(--indent-guide-offset);\n  bottom: var(--indent-guide-offset);\n  inset-inline-start: calc(1em - (var(--indent-guide-width) / 2) - 1px);\n  border-inline-end: var(--indent-guide-width) var(--indent-guide-style) var(--indent-guide-color);\n  z-index: 1;\n}\n\n@media (forced-colors: active) {\n  :host(:not([aria-disabled='true'])) .tree-item-selected .item {\n    outline: dashed 1px SelectedItem;\n  }\n}\n";

// src/components/tree-item/tree-item.ts
var WaTreeItem = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.localize = new LocalizeController(this);
    this.indeterminate = false;
    this.isLeaf = false;
    this.loading = false;
    this.selectable = false;
    this.expanded = false;
    this.selected = false;
    this.disabled = false;
    this.lazy = false;
  }
  static isTreeItem(node) {
    return node instanceof Element && node.getAttribute("role") === "treeitem";
  }
  connectedCallback() {
    super.connectedCallback();
    this.setAttribute("role", "treeitem");
    this.setAttribute("tabindex", "-1");
    if (this.isNestedItem()) {
      this.slot = "children";
    }
  }
  firstUpdated() {
    this.childrenContainer.hidden = !this.expanded;
    this.childrenContainer.style.height = this.expanded ? "auto" : "0";
    this.isLeaf = !this.lazy && this.getChildrenItems().length === 0;
    this.handleExpandedChange();
  }
  async animateCollapse() {
    this.dispatchEvent(new WaCollapseEvent());
    const duration = parseDuration(getComputedStyle(this.childrenContainer).getPropertyValue("--hide-duration"));
    await animate(
      this.childrenContainer,
      [
        // We can't animate from 'auto', so use the scroll height for now
        { height: `${this.childrenContainer.scrollHeight}px`, opacity: "1", overflow: "hidden" },
        { height: "0", opacity: "0", overflow: "hidden" }
      ],
      { duration, easing: "cubic-bezier(0.4, 0.0, 0.2, 1)" }
    );
    this.childrenContainer.hidden = true;
    this.dispatchEvent(new WaAfterCollapseEvent());
  }
  // Checks whether the item is nested into an item
  isNestedItem() {
    const parent = this.parentElement;
    return !!parent && WaTreeItem.isTreeItem(parent);
  }
  handleChildrenSlotChange() {
    this.loading = false;
    this.isLeaf = !this.lazy && this.getChildrenItems().length === 0;
  }
  willUpdate(changedProperties) {
    if (changedProperties.has("selected") && !changedProperties.has("indeterminate")) {
      this.indeterminate = false;
    }
  }
  async animateExpand() {
    this.dispatchEvent(new WaExpandEvent());
    this.childrenContainer.hidden = false;
    const duration = parseDuration(getComputedStyle(this.childrenContainer).getPropertyValue("--show-duration"));
    await animate(
      this.childrenContainer,
      [
        { height: "0", opacity: "0", overflow: "hidden" },
        { height: `${this.childrenContainer.scrollHeight}px`, opacity: "1", overflow: "hidden" }
      ],
      {
        duration,
        easing: "cubic-bezier(0.4, 0.0, 0.2, 1)"
      }
    );
    this.childrenContainer.style.height = "auto";
    this.dispatchEvent(new WaAfterExpandEvent());
  }
  handleLoadingChange() {
    this.setAttribute("aria-busy", this.loading ? "true" : "false");
    if (!this.loading) {
      this.animateExpand();
    }
  }
  handleDisabledChange() {
    this.customStates.set("disabled", this.disabled);
    this.setAttribute("aria-disabled", this.disabled ? "true" : "false");
  }
  handleExpandedState() {
    this.customStates.set("expanded", this.expanded);
  }
  handleIndeterminateStateChange() {
    this.customStates.set("indeterminate", this.indeterminate);
  }
  handleSelectedChange() {
    this.customStates.set("selected", this.selected);
    this.setAttribute("aria-selected", this.selected ? "true" : "false");
  }
  handleExpandedChange() {
    if (!this.isLeaf) {
      this.setAttribute("aria-expanded", this.expanded ? "true" : "false");
    } else {
      this.removeAttribute("aria-expanded");
    }
  }
  handleExpandAnimation() {
    if (this.expanded) {
      if (this.lazy) {
        this.loading = true;
        this.dispatchEvent(new WaLazyLoadEvent());
      } else {
        this.animateExpand();
      }
    } else {
      this.animateCollapse();
    }
  }
  handleLazyChange() {
    this.dispatchEvent(new WaLazyChangeEvent());
  }
  /** Gets all the nested tree items in this node. */
  getChildrenItems({ includeDisabled = true } = {}) {
    return this.childrenSlot ? [...this.childrenSlot.assignedElements({ flatten: true })].filter(
      (item) => WaTreeItem.isTreeItem(item) && (includeDisabled || !item.disabled)
    ) : [];
  }
  render() {
    const isRtl = this.hasUpdated ? this.localize.dir() === "rtl" : this.dir === "rtl";
    const showExpandButton = !this.loading && (!this.isLeaf || this.lazy);
    return x`
      <div
        part="base"
        class="${e2({
      "tree-item": true,
      "tree-item-expanded": this.expanded,
      "tree-item-selected": this.selected,
      "tree-item-leaf": this.isLeaf,
      "tree-item-has-expand-button": showExpandButton
    })}"
      >
        <div class="item" part="item">
          <div class="indentation" part="indentation"></div>

          <div
            part="expand-button"
            class=${e2({
      "expand-button": true,
      "expand-button-visible": showExpandButton
    })}
            aria-hidden="true"
          >
            <slot class="expand-icon-slot" name="expand-icon">
              ${n2(
      this.loading,
      () => x` <wa-spinner part="spinner" exportparts="base:spinner__base"></wa-spinner> `
    )}
              <wa-icon name=${isRtl ? "chevron-left" : "chevron-right"} library="system" variant="solid"></wa-icon>
            </slot>
            <slot class="expand-icon-slot" name="collapse-icon">
              <wa-icon name=${isRtl ? "chevron-left" : "chevron-right"} library="system" variant="solid"></wa-icon>
            </slot>
          </div>

          ${n2(
      this.selectable,
      () => x`
              <wa-checkbox
                part="checkbox"
                exportparts="
                    base:checkbox__base,
                    control:checkbox__control,
                    checked-icon:checkbox__checked-icon,
                    indeterminate-icon:checkbox__indeterminate-icon,
                    label:checkbox__label
                  "
                class="checkbox"
                ?disabled="${this.disabled}"
                ?checked="${l(this.selected)}"
                ?indeterminate="${this.indeterminate}"
                tabindex="-1"
              ></wa-checkbox>
            `
    )}

          <slot class="label" part="label"></slot>
        </div>

        <div class="children" part="children" role="group">
          <slot name="children" @slotchange="${this.handleChildrenSlotChange}"></slot>
        </div>
      </div>
    `;
  }
};
WaTreeItem.css = tree_item_default;
__decorateClass([
  r()
], WaTreeItem.prototype, "indeterminate", 2);
__decorateClass([
  r()
], WaTreeItem.prototype, "isLeaf", 2);
__decorateClass([
  r()
], WaTreeItem.prototype, "loading", 2);
__decorateClass([
  r()
], WaTreeItem.prototype, "selectable", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTreeItem.prototype, "expanded", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTreeItem.prototype, "selected", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTreeItem.prototype, "disabled", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTreeItem.prototype, "lazy", 2);
__decorateClass([
  e("slot:not([name])")
], WaTreeItem.prototype, "defaultSlot", 2);
__decorateClass([
  e("slot[name=children]")
], WaTreeItem.prototype, "childrenSlot", 2);
__decorateClass([
  e(".item")
], WaTreeItem.prototype, "itemElement", 2);
__decorateClass([
  e(".children")
], WaTreeItem.prototype, "childrenContainer", 2);
__decorateClass([
  e(".expand-button slot")
], WaTreeItem.prototype, "expandButtonSlot", 2);
__decorateClass([
  watch("loading", { waitUntilFirstUpdate: true })
], WaTreeItem.prototype, "handleLoadingChange", 1);
__decorateClass([
  watch("disabled")
], WaTreeItem.prototype, "handleDisabledChange", 1);
__decorateClass([
  watch("expanded")
], WaTreeItem.prototype, "handleExpandedState", 1);
__decorateClass([
  watch("indeterminate")
], WaTreeItem.prototype, "handleIndeterminateStateChange", 1);
__decorateClass([
  watch("selected")
], WaTreeItem.prototype, "handleSelectedChange", 1);
__decorateClass([
  watch("expanded", { waitUntilFirstUpdate: true })
], WaTreeItem.prototype, "handleExpandedChange", 1);
__decorateClass([
  watch("expanded", { waitUntilFirstUpdate: true })
], WaTreeItem.prototype, "handleExpandAnimation", 1);
__decorateClass([
  watch("lazy", { waitUntilFirstUpdate: true })
], WaTreeItem.prototype, "handleLazyChange", 1);
WaTreeItem = __decorateClass([
  t("wa-tree-item")
], WaTreeItem);

export {
  WaTreeItem
};
/*! Bundled license information:

lit-html/directives/when.js:
  (**
   * @license
   * Copyright 2021 Google LLC
   * SPDX-License-Identifier: BSD-3-Clause
   *)
*/
