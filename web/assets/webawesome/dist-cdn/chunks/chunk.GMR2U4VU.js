/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  visually_hidden_default
} from "./chunk.5PBZEKBP.js";
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
  WaErrorEvent
} from "./chunk.F3GLEBRH.js";
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

// src/events/copy.ts
var WaCopyEvent = class extends Event {
  constructor(detail) {
    super("wa-copy", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};

// src/components/copy-button/copy-button.css
var copy_button_default = ":host {\n  display: inline-block;\n  color: var(--wa-color-neutral-on-quiet);\n}\n\n.button {\n  flex: 0 0 auto;\n  display: flex;\n  align-items: center;\n  background-color: transparent;\n  border: none;\n  border-radius: var(--wa-form-control-border-radius);\n  color: inherit;\n  font-size: inherit;\n  padding: 0.5em;\n  cursor: pointer;\n  transition: color var(--wa-transition-fast) var(--wa-transition-easing);\n}\n\n@media (hover: hover) {\n  .button:hover:not([disabled]) {\n    background-color: var(--wa-color-neutral-fill-quiet);\n    color: color-mix(in oklab, currentColor, var(--wa-color-mix-hover));\n  }\n}\n\n.button:focus-visible:not([disabled]) {\n  background-color: var(--wa-color-neutral-fill-quiet);\n  color: color-mix(in oklab, currentColor, var(--wa-color-mix-hover));\n}\n\n.button:active:not([disabled]) {\n  color: color-mix(in oklab, currentColor, var(--wa-color-mix-active));\n}\n\nslot[name='success-icon'] {\n  color: var(--wa-color-success-on-quiet);\n}\n\nslot[name='error-icon'] {\n  color: var(--wa-color-danger-on-quiet);\n}\n\n.button:focus-visible {\n  outline: var(--wa-focus-ring);\n  outline-offset: var(--wa-focus-ring-offset);\n}\n\n.button[disabled] {\n  opacity: 0.5;\n  cursor: not-allowed !important;\n}\n\nslot {\n  display: inline-flex;\n}\n\n.show {\n  animation: show 100ms ease;\n}\n\n.hide {\n  animation: show 100ms ease reverse;\n}\n\n@keyframes show {\n  from {\n    scale: 0.25;\n    opacity: 0.25;\n  }\n  to {\n    scale: 1;\n    opacity: 1;\n  }\n}\n";

// src/components/copy-button/copy-button.ts
var WaCopyButton = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.localize = new LocalizeController(this);
    this.isCopying = false;
    this.status = "rest";
    this.value = "";
    this.from = "";
    this.disabled = false;
    this.copyLabel = "";
    this.successLabel = "";
    this.errorLabel = "";
    this.feedbackDuration = 1e3;
    this.tooltipPlacement = "top";
  }
  get currentLabel() {
    if (this.status === "success") {
      return this.successLabel || this.localize.term("copied");
    }
    if (this.status === "error") {
      return this.errorLabel || this.localize.term("error");
    }
    return this.copyLabel || this.localize.term("copy");
  }
  async handleCopy() {
    if (this.disabled || this.isCopying) {
      return;
    }
    this.isCopying = true;
    let valueToCopy = this.value;
    if (this.from) {
      const root = this.getRootNode();
      const isProperty = this.from.includes(".");
      const isAttribute = this.from.includes("[") && this.from.includes("]");
      let id = this.from;
      let field = "";
      if (isProperty) {
        [id, field] = this.from.trim().split(".");
      } else if (isAttribute) {
        [id, field] = this.from.trim().replace(/\]$/, "").split("[");
      }
      const target = "getElementById" in root ? root.getElementById(id) : null;
      if (target) {
        if (isAttribute) {
          valueToCopy = target.getAttribute(field) || "";
        } else if (isProperty) {
          valueToCopy = target[field] || "";
        } else {
          valueToCopy = target.textContent || "";
        }
      } else {
        this.showStatus("error");
        this.dispatchEvent(new WaErrorEvent());
      }
    }
    if (!valueToCopy) {
      this.showStatus("error");
      this.dispatchEvent(new WaErrorEvent());
    } else {
      try {
        await navigator.clipboard.writeText(valueToCopy);
        this.showStatus("success");
        this.dispatchEvent(new WaCopyEvent({ value: valueToCopy }));
      } catch (error) {
        this.showStatus("error");
        this.dispatchEvent(new WaErrorEvent());
      }
    }
  }
  async showStatus(status) {
    const iconToShow = status === "success" ? this.successIcon : this.errorIcon;
    await animateWithClass(this.copyIcon, "hide");
    this.copyIcon.hidden = true;
    this.status = status;
    iconToShow.hidden = false;
    await animateWithClass(iconToShow, "show");
    setTimeout(async () => {
      await animateWithClass(iconToShow, "hide");
      iconToShow.hidden = true;
      this.status = "rest";
      this.copyIcon.hidden = false;
      await animateWithClass(this.copyIcon, "show");
      this.isCopying = false;
    }, this.feedbackDuration);
  }
  render() {
    return x`
      <button
        class="button"
        part="button"
        type="button"
        id="copy-button"
        ?disabled=${this.disabled}
        @click=${this.handleCopy}
      >
        <!-- Render a visually hidden label to appease the accessibility checking gods -->
        <span class="wa-visually-hidden">${this.currentLabel}</span>
        <slot part="copy-icon" name="copy-icon">
          <wa-icon library="system" name="copy" variant="regular"></wa-icon>
        </slot>
        <slot part="success-icon" name="success-icon" variant="solid" hidden>
          <wa-icon library="system" name="check"></wa-icon>
        </slot>
        <slot part="error-icon" name="error-icon" variant="solid" hidden>
          <wa-icon library="system" name="xmark"></wa-icon>
        </slot>
        <wa-tooltip
          class=${e2({
      "copy-button": true,
      "copy-button-success": this.status === "success",
      "copy-button-error": this.status === "error"
    })}
          for="copy-button"
          placement=${this.tooltipPlacement}
          ?disabled=${this.disabled}
          exportparts="
            base:tooltip__base,
            base__popup:tooltip__base__popup,
            base__arrow:tooltip__base__arrow,
            body:tooltip__body
          "
          >${this.currentLabel}</wa-tooltip
        >
      </button>
    `;
  }
};
WaCopyButton.css = [visually_hidden_default, copy_button_default];
__decorateClass([
  e('slot[name="copy-icon"]')
], WaCopyButton.prototype, "copyIcon", 2);
__decorateClass([
  e('slot[name="success-icon"]')
], WaCopyButton.prototype, "successIcon", 2);
__decorateClass([
  e('slot[name="error-icon"]')
], WaCopyButton.prototype, "errorIcon", 2);
__decorateClass([
  e("wa-tooltip")
], WaCopyButton.prototype, "tooltip", 2);
__decorateClass([
  r()
], WaCopyButton.prototype, "isCopying", 2);
__decorateClass([
  r()
], WaCopyButton.prototype, "status", 2);
__decorateClass([
  n()
], WaCopyButton.prototype, "value", 2);
__decorateClass([
  n()
], WaCopyButton.prototype, "from", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaCopyButton.prototype, "disabled", 2);
__decorateClass([
  n({ attribute: "copy-label" })
], WaCopyButton.prototype, "copyLabel", 2);
__decorateClass([
  n({ attribute: "success-label" })
], WaCopyButton.prototype, "successLabel", 2);
__decorateClass([
  n({ attribute: "error-label" })
], WaCopyButton.prototype, "errorLabel", 2);
__decorateClass([
  n({ attribute: "feedback-duration", type: Number })
], WaCopyButton.prototype, "feedbackDuration", 2);
__decorateClass([
  n({ attribute: "tooltip-placement" })
], WaCopyButton.prototype, "tooltipPlacement", 2);
WaCopyButton = __decorateClass([
  t("wa-copy-button")
], WaCopyButton);

export {
  WaCopyButton
};
