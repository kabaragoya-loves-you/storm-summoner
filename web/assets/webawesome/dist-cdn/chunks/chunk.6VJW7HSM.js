/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
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
  waitForEvent
} from "./chunk.MDCY6RAP.js";
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

// src/components/details/details.css
var details_default = ":host {\n  --spacing: var(--wa-space-m);\n  --show-duration: 200ms;\n  --hide-duration: 200ms;\n\n  display: block;\n}\n\ndetails {\n  display: block;\n  overflow-anchor: none;\n  border: var(--wa-panel-border-width) var(--wa-color-surface-border) var(--wa-panel-border-style);\n  background-color: var(--wa-color-surface-default);\n  border-radius: var(--wa-panel-border-radius);\n  color: var(--wa-color-text-normal);\n\n  /* Print styles */\n  @media print {\n    background: none;\n    border: solid var(--wa-border-width-s) var(--wa-color-surface-border);\n\n    summary {\n      list-style: none;\n    }\n  }\n}\n\n/* Appearance modifiers */\n:host([appearance='plain']) details {\n  background-color: transparent;\n  border-color: transparent;\n  border-radius: 0;\n}\n\n:host([appearance='outlined']) details {\n  background-color: var(--wa-color-surface-default);\n  border-color: var(--wa-color-surface-border);\n}\n\n:host([appearance='filled']) details {\n  background-color: var(--wa-color-neutral-fill-quiet);\n  border-color: transparent;\n}\n\n:host([appearance='filled-outlined']) details {\n  background-color: var(--wa-color-neutral-fill-quiet);\n  border-color: var(--wa-color-neutral-border-quiet);\n}\n\n:host([disabled]) details {\n  opacity: 0.5;\n  cursor: not-allowed;\n}\n\nsummary {\n  display: flex;\n  align-items: center;\n  justify-content: space-between;\n  gap: var(--spacing);\n  padding: var(--spacing); /* Add padding here */\n  border-radius: calc(var(--wa-panel-border-radius) - var(--wa-panel-border-width));\n  user-select: none;\n  -webkit-user-select: none;\n  cursor: pointer;\n\n  &::marker,\n  &::-webkit-details-marker {\n    display: none;\n  }\n\n  &:focus {\n    outline: none;\n  }\n\n  &:focus-visible {\n    outline: var(--wa-focus-ring);\n    outline-offset: calc(var(--wa-panel-border-width) + var(--wa-focus-ring-offset));\n  }\n}\n\n:host([open]) summary {\n  border-end-start-radius: 0;\n  border-end-end-radius: 0;\n}\n\n/* 'Start' icon placement */\n:host([icon-placement='start']) summary {\n  flex-direction: row-reverse;\n  justify-content: start;\n}\n\n[part~='icon'] {\n  flex: 0 0 auto;\n  display: flex;\n  align-items: center;\n  color: var(--wa-color-text-quiet);\n  transition: rotate var(--wa-transition-normal) var(--wa-transition-easing);\n}\n\n:host([open]) [part~='icon'] {\n  rotate: 90deg;\n}\n\n:host([open]:dir(rtl)) [part~='icon'] {\n  rotate: -90deg;\n}\n\n:host([open]) slot[name='expand-icon'],\n:host(:not([open])) slot[name='collapse-icon'] {\n  display: none;\n}\n\n.body.animating {\n  overflow: hidden;\n}\n\n.content {\n  display: block;\n  padding-block-start: var(--spacing);\n  padding-inline: var(--spacing); /* Add horizontal padding */\n  padding-block-end: var(--spacing); /* Add bottom padding */\n}\n";

// src/components/details/details.ts
var WaDetails = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.localize = new LocalizeController(this);
    this.isAnimating = false;
    this.open = false;
    this.disabled = false;
    this.appearance = "outlined";
    this.iconPlacement = "end";
  }
  disconnectedCallback() {
    super.disconnectedCallback();
    this.detailsObserver?.disconnect();
  }
  firstUpdated() {
    this.body.style.height = this.open ? "auto" : "0";
    if (this.open) {
      this.details.open = true;
    }
    this.detailsObserver = new MutationObserver((changes) => {
      for (const change of changes) {
        if (change.type === "attributes" && change.attributeName === "open") {
          if (this.details.open) {
            this.show();
          } else {
            this.hide();
          }
        }
      }
    });
    this.detailsObserver.observe(this.details, { attributes: true });
  }
  updated(changedProperties) {
    if (changedProperties.has("isAnimating")) {
      this.customStates.set("animating", this.isAnimating);
    }
  }
  handleSummaryClick(event) {
    const eventPath = event.composedPath();
    const hasInteractiveElement = eventPath.some((element) => {
      if (!(element instanceof HTMLElement)) return false;
      const tagName = element.tagName?.toLowerCase();
      if (["a", "button", "input", "textarea", "select"].includes(tagName)) {
        return true;
      }
      if (element instanceof WebAwesomeFormAssociatedElement) {
        return !("disabled" in element) || !element.disabled;
      }
      return false;
    });
    if (hasInteractiveElement) {
      return;
    }
    event.preventDefault();
    if (!this.disabled) {
      if (this.open) {
        this.hide();
      } else {
        this.show();
      }
      this.header.focus();
    }
  }
  handleSummaryKeyDown(event) {
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      if (this.open) {
        this.hide();
      } else {
        this.show();
      }
    }
    if (event.key === "ArrowUp" || event.key === "ArrowLeft") {
      event.preventDefault();
      this.hide();
    }
    if (event.key === "ArrowDown" || event.key === "ArrowRight") {
      event.preventDefault();
      this.show();
    }
  }
  /** Closes other <wa-details> elements in the same document when they have the same name. */
  closeOthersWithSameName() {
    if (!this.name) return;
    const root = this.getRootNode();
    const otherDetails = root.querySelectorAll(`wa-details[name="${this.name}"]`);
    otherDetails.forEach((detail) => {
      if (detail !== this && detail.open) {
        detail.open = false;
      }
    });
  }
  async handleOpenChange() {
    if (this.open) {
      this.details.open = true;
      const waShow = new WaShowEvent();
      this.dispatchEvent(waShow);
      if (waShow.defaultPrevented) {
        this.open = false;
        this.details.open = false;
        return;
      }
      this.closeOthersWithSameName();
      this.isAnimating = true;
      const duration = parseDuration(getComputedStyle(this.body).getPropertyValue("--show-duration"));
      await animate(
        this.body,
        [
          { height: "0", opacity: "0" },
          { height: `${this.body.scrollHeight}px`, opacity: "1" }
        ],
        {
          duration,
          easing: "linear"
        }
      );
      this.body.style.height = "auto";
      this.isAnimating = false;
      this.dispatchEvent(new WaAfterShowEvent());
    } else {
      const waHide = new WaHideEvent();
      this.dispatchEvent(waHide);
      if (waHide.defaultPrevented) {
        this.details.open = true;
        this.open = true;
        return;
      }
      this.isAnimating = true;
      const duration = parseDuration(getComputedStyle(this.body).getPropertyValue("--hide-duration"));
      await animate(
        this.body,
        [
          { height: `${this.body.scrollHeight}px`, opacity: "1" },
          { height: "0", opacity: "0" }
        ],
        { duration, easing: "linear" }
      );
      this.body.style.height = "auto";
      this.isAnimating = false;
      this.details.open = false;
      this.dispatchEvent(new WaAfterHideEvent());
    }
  }
  /** Shows the details. */
  async show() {
    if (this.open || this.disabled) {
      return void 0;
    }
    this.open = true;
    return waitForEvent(this, "wa-after-show");
  }
  /** Hides the details */
  async hide() {
    if (!this.open || this.disabled) {
      return void 0;
    }
    this.open = false;
    return waitForEvent(this, "wa-after-hide");
  }
  render() {
    const isRtl = !this.hasUpdated ? this.dir === "rtl" : this.localize.dir() === "rtl";
    return x`
      <details part="base">
        <summary
          part="header"
          role="button"
          aria-expanded=${this.open ? "true" : "false"}
          aria-controls="content"
          aria-disabled=${this.disabled ? "true" : "false"}
          tabindex=${this.disabled ? "-1" : "0"}
          @click=${this.handleSummaryClick}
          @keydown=${this.handleSummaryKeyDown}
        >
          <slot name="summary" part="summary">${this.summary}</slot>

          <span part="icon">
            <slot name="expand-icon">
              <wa-icon library="system" variant="solid" name=${isRtl ? "chevron-left" : "chevron-right"}></wa-icon>
            </slot>
            <slot name="collapse-icon">
              <wa-icon library="system" variant="solid" name=${isRtl ? "chevron-left" : "chevron-right"}></wa-icon>
            </slot>
          </span>
        </summary>

        <div
          class=${e2({
      body: true,
      animating: this.isAnimating
    })}
          role="region"
          aria-labelledby="header"
        >
          <slot part="content" id="content" class="content"></slot>
        </div>
      </details>
    `;
  }
};
WaDetails.css = details_default;
__decorateClass([
  e("details")
], WaDetails.prototype, "details", 2);
__decorateClass([
  e("summary")
], WaDetails.prototype, "header", 2);
__decorateClass([
  e(".body")
], WaDetails.prototype, "body", 2);
__decorateClass([
  e(".expand-icon-slot")
], WaDetails.prototype, "expandIconSlot", 2);
__decorateClass([
  r()
], WaDetails.prototype, "isAnimating", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaDetails.prototype, "open", 2);
__decorateClass([
  n()
], WaDetails.prototype, "summary", 2);
__decorateClass([
  n({ reflect: true })
], WaDetails.prototype, "name", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaDetails.prototype, "disabled", 2);
__decorateClass([
  n({ reflect: true })
], WaDetails.prototype, "appearance", 2);
__decorateClass([
  n({ attribute: "icon-placement", reflect: true })
], WaDetails.prototype, "iconPlacement", 2);
__decorateClass([
  watch("open", { waitUntilFirstUpdate: true })
], WaDetails.prototype, "handleOpenChange", 1);
WaDetails = __decorateClass([
  t("wa-details")
], WaDetails);

export {
  WaDetails
};
