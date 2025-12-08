/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  lockBodyScrolling,
  unlockBodyScrolling
} from "./chunk.GB2I2DTW.js";
import {
  parseSpaceDelimitedTokens
} from "./chunk.AFYG3E5E.js";
import {
  WaAfterHideEvent,
  WaAfterShowEvent,
  WaHideEvent,
  WaShowEvent
} from "./chunk.DTCJCEYF.js";
import {
  HasSlotController
} from "./chunk.SFADIYDM.js";
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
  WebAwesomeElement,
  e,
  n,
  o,
  t
} from "./chunk.7F6EHFYD.js";
import {
  x
} from "./chunk.23WMFJHA.js";
import {
  __decorateClass
} from "./chunk.6E4D3PD7.js";

// src/components/dialog/dialog.css
var dialog_default = ":host {\n  --width: 31rem;\n  --spacing: var(--wa-space-l);\n  --show-duration: 200ms;\n  --hide-duration: 200ms;\n\n  display: none;\n}\n\n:host([open]) {\n  display: block;\n}\n\n.dialog {\n  display: flex;\n  flex-direction: column;\n  top: 0;\n  right: 0;\n  bottom: 0;\n  left: 0;\n  width: var(--width);\n  max-width: calc(100% - var(--wa-space-2xl));\n  max-height: calc(100% - var(--wa-space-2xl));\n  background-color: var(--wa-color-surface-raised);\n  border-radius: var(--wa-panel-border-radius);\n  border: none;\n  box-shadow: var(--wa-shadow-l);\n  padding: 0;\n  margin: auto;\n\n  &.show {\n    animation: show-dialog var(--show-duration) ease;\n\n    &::backdrop {\n      animation: show-backdrop var(--show-duration, 200ms) ease;\n    }\n  }\n\n  &.hide {\n    animation: show-dialog var(--hide-duration) ease reverse;\n\n    &::backdrop {\n      animation: show-backdrop var(--hide-duration, 200ms) ease reverse;\n    }\n  }\n\n  &.pulse {\n    animation: pulse 250ms ease;\n  }\n}\n\n.dialog:focus {\n  outline: none;\n}\n\n/* Ensure there's enough vertical padding for phones that don't update vh when chrome appears (e.g. iPhone) */\n@media screen and (max-width: 420px) {\n  .dialog {\n    max-height: 80vh;\n  }\n}\n\n.open {\n  display: flex;\n  opacity: 1;\n}\n\n.header {\n  flex: 0 0 auto;\n  display: flex;\n  flex-wrap: nowrap;\n\n  padding-inline-start: var(--spacing);\n  padding-block-end: 0;\n\n  /* Subtract the close button's padding so that the X is visually aligned with the edges of the dialog content */\n  padding-inline-end: calc(var(--spacing) - var(--wa-form-control-padding-block));\n  padding-block-start: calc(var(--spacing) - var(--wa-form-control-padding-block));\n}\n\n.title {\n  align-self: center;\n  flex: 1 1 auto;\n  font-family: inherit;\n  font-size: var(--wa-font-size-l);\n  font-weight: var(--wa-font-weight-heading);\n  line-height: var(--wa-line-height-condensed);\n  margin: 0;\n}\n\n.header-actions {\n  align-self: start;\n  display: flex;\n  flex-shrink: 0;\n  flex-wrap: wrap;\n  justify-content: end;\n  gap: var(--wa-space-2xs);\n  padding-inline-start: var(--spacing);\n}\n\n.header-actions wa-button,\n.header-actions ::slotted(wa-button) {\n  flex: 0 0 auto;\n  display: flex;\n  align-items: center;\n}\n\n.body {\n  flex: 1 1 auto;\n  display: block;\n  padding: var(--spacing);\n  overflow: auto;\n  -webkit-overflow-scrolling: touch;\n\n  &:focus {\n    outline: none;\n  }\n\n  &:focus-visible {\n    outline: var(--wa-focus-ring);\n    outline-offset: var(--wa-focus-ring-offset);\n  }\n}\n\n.footer {\n  flex: 0 0 auto;\n  display: flex;\n  flex-wrap: wrap;\n  gap: var(--wa-space-xs);\n  justify-content: end;\n  padding: var(--spacing);\n  padding-block-start: 0;\n}\n\n.footer ::slotted(wa-button:not(:first-of-type)) {\n  margin-inline-start: var(--wa-spacing-xs);\n}\n\n.dialog::backdrop {\n  /*\n    NOTE: the ::backdrop element doesn't inherit properly in Safari yet, but it will in 17.4! At that time, we can\n    remove the fallback values here.\n  */\n  background-color: var(--wa-color-overlay-modal, rgb(0 0 0 / 0.25));\n}\n\n@keyframes pulse {\n  0% {\n    scale: 1;\n  }\n  50% {\n    scale: 1.02;\n  }\n  100% {\n    scale: 1;\n  }\n}\n\n@keyframes show-dialog {\n  from {\n    opacity: 0;\n    scale: 0.8;\n  }\n  to {\n    opacity: 1;\n    scale: 1;\n  }\n}\n\n@keyframes show-backdrop {\n  from {\n    opacity: 0;\n  }\n  to {\n    opacity: 1;\n  }\n}\n\n@media (forced-colors: active) {\n  .dialog {\n    border: solid 1px white;\n  }\n}\n";

// src/components/dialog/dialog.ts
var WaDialog = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.localize = new LocalizeController(this);
    this.hasSlotController = new HasSlotController(this, "footer", "header-actions", "label");
    this.open = false;
    this.label = "";
    this.withoutHeader = false;
    this.lightDismiss = false;
    this.handleDocumentKeyDown = (event) => {
      if (event.key === "Escape" && this.open) {
        event.preventDefault();
        event.stopPropagation();
        this.requestClose(this.dialog);
      }
    };
  }
  firstUpdated() {
    if (this.open) {
      this.addOpenListeners();
      this.dialog.showModal();
      lockBodyScrolling(this);
    }
  }
  disconnectedCallback() {
    super.disconnectedCallback();
    unlockBodyScrolling(this);
    this.removeOpenListeners();
  }
  async requestClose(source) {
    const waHideEvent = new WaHideEvent({ source });
    this.dispatchEvent(waHideEvent);
    if (waHideEvent.defaultPrevented) {
      this.open = true;
      animateWithClass(this.dialog, "pulse");
      return;
    }
    this.removeOpenListeners();
    await animateWithClass(this.dialog, "hide");
    this.open = false;
    this.dialog.close();
    unlockBodyScrolling(this);
    const trigger = this.originalTrigger;
    if (typeof trigger?.focus === "function") {
      setTimeout(() => trigger.focus());
    }
    this.dispatchEvent(new WaAfterHideEvent());
  }
  addOpenListeners() {
    document.addEventListener("keydown", this.handleDocumentKeyDown);
  }
  removeOpenListeners() {
    document.removeEventListener("keydown", this.handleDocumentKeyDown);
  }
  handleDialogCancel(event) {
    event.preventDefault();
    if (!this.dialog.classList.contains("hide") && event.target === this.dialog) {
      this.requestClose(this.dialog);
    }
  }
  handleDialogClick(event) {
    const target = event.target;
    const button = target.closest('[data-dialog="close"]');
    if (button) {
      event.stopPropagation();
      this.requestClose(button);
    }
  }
  async handleDialogPointerDown(event) {
    if (event.target === this.dialog) {
      if (this.lightDismiss) {
        this.requestClose(this.dialog);
      } else {
        await animateWithClass(this.dialog, "pulse");
      }
    }
  }
  handleOpenChange() {
    if (this.open && !this.dialog.open) {
      this.show();
    } else if (!this.open && this.dialog.open) {
      this.open = true;
      this.requestClose(this.dialog);
    }
  }
  /** Shows the dialog. */
  async show() {
    const waShowEvent = new WaShowEvent();
    this.dispatchEvent(waShowEvent);
    if (waShowEvent.defaultPrevented) {
      this.open = false;
      return;
    }
    this.addOpenListeners();
    this.originalTrigger = document.activeElement;
    this.open = true;
    this.dialog.showModal();
    lockBodyScrolling(this);
    requestAnimationFrame(() => {
      const elementToFocus = this.querySelector("[autofocus]");
      if (elementToFocus && typeof elementToFocus.focus === "function") {
        elementToFocus.focus();
      } else {
        this.dialog.focus();
      }
    });
    await animateWithClass(this.dialog, "show");
    this.dispatchEvent(new WaAfterShowEvent());
  }
  render() {
    const hasHeader = !this.withoutHeader;
    const hasFooter = this.hasSlotController.test("footer");
    return x`
      <dialog
        part="dialog"
        class=${e2({
      dialog: true,
      open: this.open
    })}
        @cancel=${this.handleDialogCancel}
        @click=${this.handleDialogClick}
        @pointerdown=${this.handleDialogPointerDown}
      >
        ${hasHeader ? x`
              <header part="header" class="header">
                <h2 part="title" class="title" id="title">
                  <!-- If there's no label, use an invisible character to prevent the header from collapsing -->
                  <slot name="label"> ${this.label.length > 0 ? this.label : String.fromCharCode(8203)} </slot>
                </h2>
                <div part="header-actions" class="header-actions">
                  <slot name="header-actions"></slot>
                  <wa-button
                    part="close-button"
                    exportparts="base:close-button__base"
                    class="close"
                    appearance="plain"
                    @click="${(event) => this.requestClose(event.target)}"
                  >
                    <wa-icon
                      name="xmark"
                      label=${this.localize.term("close")}
                      library="system"
                      variant="solid"
                    ></wa-icon>
                  </wa-button>
                </div>
              </header>
            ` : ""}

        <div part="body" class="body"><slot></slot></div>

        ${hasFooter ? x`
              <footer part="footer" class="footer">
                <slot name="footer"></slot>
              </footer>
            ` : ""}
      </dialog>
    `;
  }
};
WaDialog.css = dialog_default;
__decorateClass([
  e(".dialog")
], WaDialog.prototype, "dialog", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaDialog.prototype, "open", 2);
__decorateClass([
  n({ reflect: true })
], WaDialog.prototype, "label", 2);
__decorateClass([
  n({ attribute: "without-header", type: Boolean, reflect: true })
], WaDialog.prototype, "withoutHeader", 2);
__decorateClass([
  n({ attribute: "light-dismiss", type: Boolean })
], WaDialog.prototype, "lightDismiss", 2);
__decorateClass([
  watch("open", { waitUntilFirstUpdate: true })
], WaDialog.prototype, "handleOpenChange", 1);
WaDialog = __decorateClass([
  t("wa-dialog")
], WaDialog);
document.addEventListener("click", (event) => {
  const dialogAttrEl = event.target.closest("[data-dialog]");
  if (dialogAttrEl instanceof Element) {
    const [command, id] = parseSpaceDelimitedTokens(dialogAttrEl.getAttribute("data-dialog") || "");
    if (command === "open" && id?.length) {
      const doc = dialogAttrEl.getRootNode();
      const dialog = doc.getElementById(id);
      if (dialog?.localName === "wa-dialog") {
        dialog.open = true;
      } else {
        console.warn(`A dialog with an ID of "${id}" could not be found in this document.`);
      }
    }
  }
});
if (!o) {
  document.addEventListener("pointerdown", () => {
  });
}

export {
  WaDialog
};
