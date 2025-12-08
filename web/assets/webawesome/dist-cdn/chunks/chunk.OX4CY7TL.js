/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  WaPopup
} from "./chunk.PTO7B276.js";
import {
  WaAfterHideEvent,
  WaAfterShowEvent,
  WaHideEvent,
  WaShowEvent
} from "./chunk.DTCJCEYF.js";
import {
  uniqueId
} from "./chunk.KABXVFSA.js";
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

// src/components/popover/popover.css
var popover_default = ":host {\n  --arrow-size: 0.375rem;\n  --max-width: 25rem;\n  --show-duration: 100ms;\n  --hide-duration: 100ms;\n\n  /* Internal calculated properties */\n  --arrow-diagonal-size: calc((var(--arrow-size) * sin(45deg)));\n\n  display: contents;\n\n  /** Defaults for inherited CSS properties */\n  font-size: var(--wa-font-size-m);\n  line-height: var(--wa-line-height-normal);\n  text-align: start;\n  white-space: normal;\n}\n\n/* The native dialog element */\n.dialog {\n  display: none;\n  position: fixed;\n  inset: 0;\n  width: 100%;\n  height: 100%;\n  margin: 0;\n  padding: 0;\n  border: none;\n  background: transparent;\n  overflow: visible;\n  pointer-events: none;\n\n  &:focus {\n    outline: none;\n  }\n\n  &[open] {\n    display: block;\n  }\n}\n\n/* The <wa-popup> element */\n.popover {\n  --arrow-size: inherit;\n  --show-duration: inherit;\n  --hide-duration: inherit;\n\n  pointer-events: auto;\n\n  &::part(arrow) {\n    background-color: var(--wa-color-surface-default);\n    border-top: none;\n    border-left: none;\n    border-bottom: solid var(--wa-panel-border-width) var(--wa-color-surface-border);\n    border-right: solid var(--wa-panel-border-width) var(--wa-color-surface-border);\n    box-shadow: none;\n  }\n}\n\n.popover[placement^='top']::part(popup) {\n  transform-origin: bottom;\n}\n\n.popover[placement^='bottom']::part(popup) {\n  transform-origin: top;\n}\n\n.popover[placement^='left']::part(popup) {\n  transform-origin: right;\n}\n\n.popover[placement^='right']::part(popup) {\n  transform-origin: left;\n}\n\n/* Body */\n.body {\n  display: flex;\n  flex-direction: column;\n  width: max-content;\n  max-width: var(--max-width);\n  padding: var(--wa-space-l);\n  background-color: var(--wa-color-surface-default);\n  border: var(--wa-panel-border-width) solid var(--wa-color-surface-border);\n  border-radius: var(--wa-panel-border-radius);\n  border-style: var(--wa-panel-border-style);\n  box-shadow: var(--wa-shadow-l);\n  color: var(--wa-color-text-normal);\n  user-select: none;\n  -webkit-user-select: none;\n}\n";

// src/components/popover/popover.ts
var openPopovers = /* @__PURE__ */ new Set();
var WaPopover = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.anchor = null;
    this.placement = "top";
    this.open = false;
    this.distance = 8;
    this.skidding = 0;
    this.for = null;
    this.withoutArrow = false;
    this.eventController = new AbortController();
    this.handleAnchorClick = () => {
      this.open = !this.open;
    };
    this.handleBodyClick = (event) => {
      const target = event.target;
      const button = target.closest('[data-popover="close"]');
      if (button) {
        event.stopPropagation();
        this.open = false;
      }
    };
    this.handleDocumentKeyDown = (event) => {
      if (event.key === "Escape") {
        event.preventDefault();
        this.open = false;
        if (this.anchor && typeof this.anchor.focus === "function") {
          this.anchor.focus();
        }
      }
    };
    this.handleDocumentClick = (event) => {
      const target = event.target;
      if (this.anchor && event.composedPath().includes(this.anchor)) {
        return;
      }
      if (target.closest("wa-popover") !== this) {
        this.open = false;
      }
    };
  }
  connectedCallback() {
    super.connectedCallback();
    if (!this.id) {
      this.id = uniqueId("wa-popover-");
    }
  }
  disconnectedCallback() {
    super.disconnectedCallback();
    document.removeEventListener("keydown", this.handleDocumentKeyDown);
    this.eventController.abort();
  }
  firstUpdated() {
    if (this.open) {
      this.dialog.show();
      this.popup.active = true;
      this.popup.reposition();
    }
  }
  updated(changedProperties) {
    if (changedProperties.has("open")) {
      this.customStates.set("open", this.open);
    }
  }
  async handleOpenChange() {
    if (this.open) {
      const waShowEvent = new WaShowEvent();
      this.dispatchEvent(waShowEvent);
      if (waShowEvent.defaultPrevented) {
        this.open = false;
        return;
      }
      openPopovers.forEach((popover) => popover.open = false);
      document.addEventListener("keydown", this.handleDocumentKeyDown, { signal: this.eventController.signal });
      document.addEventListener("click", this.handleDocumentClick, { signal: this.eventController.signal });
      this.dialog.show();
      this.popup.active = true;
      openPopovers.add(this);
      requestAnimationFrame(() => {
        const elementToFocus = this.querySelector("[autofocus]");
        if (elementToFocus && typeof elementToFocus.focus === "function") {
          elementToFocus.focus();
        } else {
          this.dialog.focus();
        }
      });
      await animateWithClass(this.popup.popup, "show-with-scale");
      this.popup.reposition();
      this.dispatchEvent(new WaAfterShowEvent());
    } else {
      const waHideEvent = new WaHideEvent();
      this.dispatchEvent(waHideEvent);
      if (waHideEvent.defaultPrevented) {
        this.open = true;
        return;
      }
      document.removeEventListener("keydown", this.handleDocumentKeyDown);
      document.removeEventListener("click", this.handleDocumentClick);
      openPopovers.delete(this);
      await animateWithClass(this.popup.popup, "hide-with-scale");
      this.popup.active = false;
      this.dialog.close();
      this.dispatchEvent(new WaAfterHideEvent());
    }
  }
  handleForChange() {
    const rootNode = this.getRootNode();
    if (!rootNode) {
      return;
    }
    const newAnchor = this.for ? rootNode.getElementById(this.for) : null;
    const oldAnchor = this.anchor;
    if (newAnchor === oldAnchor) {
      return;
    }
    const { signal } = this.eventController;
    if (newAnchor) {
      newAnchor.addEventListener("click", this.handleAnchorClick, { signal });
    }
    if (oldAnchor) {
      oldAnchor.removeEventListener("click", this.handleAnchorClick);
    }
    this.anchor = newAnchor;
    if (this.for && !newAnchor) {
      console.warn(
        `A popover was assigned to an element with an ID of "${this.for}" but the element could not be found.`,
        this
      );
    }
  }
  async handleOptionsChange() {
    if (this.hasUpdated) {
      await this.updateComplete;
      this.popup.reposition();
    }
  }
  /** Shows the popover. */
  async show() {
    if (this.open) {
      return void 0;
    }
    this.open = true;
    return waitForEvent(this, "wa-after-show");
  }
  /** Hides the popover. */
  async hide() {
    if (!this.open) {
      return void 0;
    }
    this.open = false;
    return waitForEvent(this, "wa-after-hide");
  }
  render() {
    return x`
      <dialog part="dialog" class="dialog">
        <wa-popup
          part="popup"
          exportparts="
            popup:popup__popup,
            arrow:popup__arrow
          "
          class=${e2({
      popover: true,
      "popover-open": this.open
    })}
          placement=${this.placement}
          distance=${this.distance}
          skidding=${this.skidding}
          flip
          shift
          ?arrow=${!this.withoutArrow}
          .anchor=${this.anchor}
        >
          <div part="body" class="body" @click=${this.handleBodyClick}>
            <slot></slot>
          </div>
        </wa-popup>
      </dialog>
    `;
  }
};
WaPopover.css = popover_default;
WaPopover.dependencies = { "wa-popup": WaPopup };
__decorateClass([
  e("dialog")
], WaPopover.prototype, "dialog", 2);
__decorateClass([
  e(".body")
], WaPopover.prototype, "body", 2);
__decorateClass([
  e("wa-popup")
], WaPopover.prototype, "popup", 2);
__decorateClass([
  r()
], WaPopover.prototype, "anchor", 2);
__decorateClass([
  n()
], WaPopover.prototype, "placement", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaPopover.prototype, "open", 2);
__decorateClass([
  n({ type: Number })
], WaPopover.prototype, "distance", 2);
__decorateClass([
  n({ type: Number })
], WaPopover.prototype, "skidding", 2);
__decorateClass([
  n()
], WaPopover.prototype, "for", 2);
__decorateClass([
  n({ attribute: "without-arrow", type: Boolean, reflect: true })
], WaPopover.prototype, "withoutArrow", 2);
__decorateClass([
  watch("open", { waitUntilFirstUpdate: true })
], WaPopover.prototype, "handleOpenChange", 1);
__decorateClass([
  watch("for")
], WaPopover.prototype, "handleForChange", 1);
__decorateClass([
  watch(["distance", "placement", "skidding"])
], WaPopover.prototype, "handleOptionsChange", 1);
WaPopover = __decorateClass([
  t("wa-popover")
], WaPopover);

export {
  WaPopover
};
