/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  MirrorValidator
} from "./chunk.47AP2JNT.js";
import {
  WaInvalidEvent,
  WebAwesomeFormAssociatedElement
} from "./chunk.D4WZJ7B2.js";
import {
  o
} from "./chunk.G3G3UFWM.js";
import {
  variants_default
} from "./chunk.R6O3GHMM.js";
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
  LocalizeController
} from "./chunk.DR3YY3XN.js";
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
  b,
  w,
  x
} from "./chunk.23WMFJHA.js";
import {
  __decorateClass
} from "./chunk.6E4D3PD7.js";

// ../../node_modules/lit-html/static.js
var a = Symbol.for("");
var o2 = (t2) => {
  if (t2?.r === a) return t2?._$litStatic$;
};
var i = (t2, ...r2) => ({ _$litStatic$: r2.reduce(((r3, e3, a2) => r3 + ((t3) => {
  if (void 0 !== t3._$litStatic$) return t3._$litStatic$;
  throw Error(`Value passed to 'literal' function must be a 'literal' result: ${t3}. Use 'unsafeStatic' to pass non-literal values, but
            take care to ensure page security.`);
})(e3) + t2[a2 + 1]), t2[0]), r: a });
var l = /* @__PURE__ */ new Map();
var n2 = (t2) => (r2, ...e3) => {
  const a2 = e3.length;
  let s, i2;
  const n3 = [], u2 = [];
  let c2, $2 = 0, f = false;
  for (; $2 < a2; ) {
    for (c2 = r2[$2]; $2 < a2 && void 0 !== (i2 = e3[$2], s = o2(i2)); ) c2 += s + r2[++$2], f = true;
    $2 !== a2 && u2.push(i2), n3.push(c2), $2++;
  }
  if ($2 === a2 && n3.push(r2[a2]), f) {
    const t3 = n3.join("$$lit$$");
    void 0 === (r2 = l.get(t3)) && (n3.raw = n3, l.set(t3, r2 = n3)), e3 = u2;
  }
  return t2(r2, ...e3);
};
var u = n2(x);
var c = n2(b);
var $ = n2(w);

// src/components/button/button.css
var button_default = "@layer wa-component {\n  :host {\n    display: inline-block;\n\n    /* Workaround because Chrome doesn't like :host(:has()) below\n     * https://issues.chromium.org/issues/40062355\n     * Firefox doesn't like this nested rule, so both are needed */\n    &:has(wa-badge) {\n      position: relative;\n    }\n  }\n\n  /* Apply relative positioning only when needed to position wa-badge\n   * This avoids creating a new stacking context for every button */\n  :host(:has(wa-badge)) {\n    position: relative;\n  }\n}\n\n.button {\n  display: inline-flex;\n  align-items: center;\n  justify-content: center;\n  text-decoration: none;\n  user-select: none;\n  -webkit-user-select: none;\n  white-space: nowrap;\n  vertical-align: middle;\n  transition-property: background, border, box-shadow, color;\n  transition-duration: var(--wa-transition-fast);\n  transition-timing-function: var(--wa-transition-easing);\n  cursor: pointer;\n  padding: 0 var(--wa-form-control-padding-inline);\n  font-family: inherit;\n  font-size: inherit;\n  font-weight: var(--wa-font-weight-action);\n  line-height: calc(var(--wa-form-control-height) - var(--border-width) * 2);\n  height: var(--wa-form-control-height);\n  width: 100%;\n\n  background-color: var(--wa-color-fill-loud, var(--wa-color-neutral-fill-loud));\n  border-color: transparent;\n  color: var(--wa-color-on-loud, var(--wa-color-neutral-on-loud));\n  border-radius: var(--wa-form-control-border-radius);\n  border-style: var(--wa-border-style);\n  border-width: var(--wa-border-width-s);\n}\n\n/* Appearance modifiers */\n:host([appearance='plain']) {\n  .button {\n    color: var(--wa-color-on-quiet, var(--wa-color-neutral-on-quiet));\n    background-color: transparent;\n    border-color: transparent;\n  }\n  @media (hover: hover) {\n    .button:not(.disabled):not(.loading):hover {\n      color: var(--wa-color-on-quiet, var(--wa-color-neutral-on-quiet));\n      background-color: var(--wa-color-fill-quiet, var(--wa-color-neutral-fill-quiet));\n    }\n  }\n  .button:not(.disabled):not(.loading):active {\n    color: var(--wa-color-on-quiet, var(--wa-color-neutral-on-quiet));\n    background-color: color-mix(\n      in oklab,\n      var(--wa-color-fill-quiet, var(--wa-color-neutral-fill-quiet)),\n      var(--wa-color-mix-active)\n    );\n  }\n}\n\n:host([appearance='outlined']) {\n  .button {\n    color: var(--wa-color-on-quiet, var(--wa-color-neutral-on-quiet));\n    background-color: transparent;\n    border-color: var(--wa-color-border-loud, var(--wa-color-neutral-border-loud));\n  }\n  @media (hover: hover) {\n    .button:not(.disabled):not(.loading):hover {\n      color: var(--wa-color-on-quiet, var(--wa-color-neutral-on-quiet));\n      background-color: var(--wa-color-fill-quiet, var(--wa-color-neutral-fill-quiet));\n    }\n  }\n  .button:not(.disabled):not(.loading):active {\n    color: var(--wa-color-on-quiet, var(--wa-color-neutral-on-quiet));\n    background-color: color-mix(\n      in oklab,\n      var(--wa-color-fill-quiet, var(--wa-color-neutral-fill-quiet)),\n      var(--wa-color-mix-active)\n    );\n  }\n}\n\n:host([appearance='filled']) {\n  .button {\n    color: var(--wa-color-on-normal, var(--wa-color-neutral-on-normal));\n    background-color: var(--wa-color-fill-normal, var(--wa-color-neutral-fill-normal));\n    border-color: transparent;\n  }\n  @media (hover: hover) {\n    .button:not(.disabled):not(.loading):hover {\n      color: var(--wa-color-on-normal, var(--wa-color-neutral-on-normal));\n      background-color: color-mix(\n        in oklab,\n        var(--wa-color-fill-normal, var(--wa-color-neutral-fill-normal)),\n        var(--wa-color-mix-hover)\n      );\n    }\n  }\n  .button:not(.disabled):not(.loading):active {\n    color: var(--wa-color-on-normal, var(--wa-color-neutral-on-normal));\n    background-color: color-mix(\n      in oklab,\n      var(--wa-color-fill-normal, var(--wa-color-neutral-fill-normal)),\n      var(--wa-color-mix-active)\n    );\n  }\n}\n\n:host([appearance='filled-outlined']) {\n  .button {\n    color: var(--wa-color-on-normal, var(--wa-color-neutral-on-normal));\n    background-color: var(--wa-color-fill-normal, var(--wa-color-neutral-fill-normal));\n    border-color: var(--wa-color-border-normal, var(--wa-color-neutral-border-normal));\n  }\n  @media (hover: hover) {\n    .button:not(.disabled):not(.loading):hover {\n      color: var(--wa-color-on-normal, var(--wa-color-neutral-on-normal));\n      background-color: color-mix(\n        in oklab,\n        var(--wa-color-fill-normal, var(--wa-color-neutral-fill-normal)),\n        var(--wa-color-mix-hover)\n      );\n    }\n  }\n  .button:not(.disabled):not(.loading):active {\n    color: var(--wa-color-on-normal, var(--wa-color-neutral-on-normal));\n    background-color: color-mix(\n      in oklab,\n      var(--wa-color-fill-normal, var(--wa-color-neutral-fill-normal)),\n      var(--wa-color-mix-active)\n    );\n  }\n}\n\n:host([appearance='accent']) {\n  .button {\n    color: var(--wa-color-on-loud, var(--wa-color-neutral-on-loud));\n    background-color: var(--wa-color-fill-loud, var(--wa-color-neutral-fill-loud));\n    border-color: transparent;\n  }\n  @media (hover: hover) {\n    .button:not(.disabled):not(.loading):hover {\n      background-color: color-mix(\n        in oklab,\n        var(--wa-color-fill-loud, var(--wa-color-neutral-fill-loud)),\n        var(--wa-color-mix-hover)\n      );\n    }\n  }\n  .button:not(.disabled):not(.loading):active {\n    background-color: color-mix(\n      in oklab,\n      var(--wa-color-fill-loud, var(--wa-color-neutral-fill-loud)),\n      var(--wa-color-mix-active)\n    );\n  }\n}\n\n/* Focus states */\n.button:focus {\n  outline: none;\n}\n\n.button:focus-visible {\n  outline: var(--wa-focus-ring);\n  outline-offset: var(--wa-focus-ring-offset);\n}\n\n/* Disabled state */\n.button.disabled {\n  opacity: 0.5;\n  cursor: not-allowed;\n}\n\n/* When disabled, prevent mouse events from bubbling up from children */\n.button.disabled * {\n  pointer-events: none;\n}\n\n/* Keep it last so Safari doesn't stop parsing this block */\n.button::-moz-focus-inner {\n  border: 0;\n}\n\n/* Icon buttons */\n.button.is-icon-button {\n  outline-offset: 2px;\n  width: var(--wa-form-control-height);\n  aspect-ratio: 1;\n}\n\n.button.is-icon-button:has(wa-icon) {\n  width: auto;\n}\n\n/* Pill modifier */\n:host([pill]) .button {\n  border-radius: var(--wa-border-radius-pill);\n}\n\n/*\n * Label\n */\n\n.start,\n.end {\n  flex: 0 0 auto;\n  display: flex;\n  align-items: center;\n  pointer-events: none;\n}\n\n.label {\n  display: inline-block;\n}\n\n.is-icon-button .label {\n  display: flex;\n}\n\n.label::slotted(wa-icon) {\n  align-self: center;\n}\n\n/*\n * Caret modifier\n */\n\nwa-icon[part='caret'] {\n  display: flex;\n  align-self: center;\n  align-items: center;\n\n  &::part(svg) {\n    width: 0.875em;\n    height: 0.875em;\n  }\n\n  .button:has(&) .end {\n    display: none;\n  }\n}\n\n/*\n * Loading modifier\n */\n\n.loading {\n  position: relative;\n  cursor: wait;\n\n  .start,\n  .label,\n  .end,\n  .caret {\n    visibility: hidden;\n  }\n\n  wa-spinner {\n    --indicator-color: currentColor;\n    --track-color: color-mix(in oklab, currentColor, transparent 90%);\n\n    position: absolute;\n    font-size: 1em;\n    height: 1em;\n    width: 1em;\n    top: calc(50% - 0.5em);\n    left: calc(50% - 0.5em);\n  }\n}\n\n/*\n * Badges\n */\n\n.button ::slotted(wa-badge) {\n  border-color: var(--wa-color-surface-default);\n  position: absolute;\n  inset-block-start: 0;\n  inset-inline-end: 0;\n  translate: 50% -50%;\n  pointer-events: none;\n}\n\n:host(:dir(rtl)) ::slotted(wa-badge) {\n  translate: -50% -50%;\n}\n\n/*\n* Button spacing\n*/\n\nslot[name='start']::slotted(*) {\n  margin-inline-end: 0.75em;\n}\n\nslot[name='end']::slotted(*),\n.button:not(.visually-hidden-label) [part='caret'] {\n  margin-inline-start: 0.75em;\n}\n\n/*\n * Button group border radius modifications\n */\n\n/* Remove border radius from all grouped buttons by default */\n:host(.wa-button-group__button) .button {\n  border-radius: 0;\n}\n\n/* Horizontal orientation */\n:host(.wa-button-group__horizontal.wa-button-group__button-first) .button {\n  border-start-start-radius: var(--wa-form-control-border-radius);\n  border-end-start-radius: var(--wa-form-control-border-radius);\n}\n\n:host(.wa-button-group__horizontal.wa-button-group__button-last) .button {\n  border-start-end-radius: var(--wa-form-control-border-radius);\n  border-end-end-radius: var(--wa-form-control-border-radius);\n}\n\n/* Vertical orientation */\n:host(.wa-button-group__vertical) {\n  flex: 1 1 auto;\n}\n\n:host(.wa-button-group__vertical) .button {\n  width: 100%;\n  justify-content: start;\n}\n\n:host(.wa-button-group__vertical.wa-button-group__button-first) .button {\n  border-start-start-radius: var(--wa-form-control-border-radius);\n  border-start-end-radius: var(--wa-form-control-border-radius);\n}\n\n:host(.wa-button-group__vertical.wa-button-group__button-last) .button {\n  border-end-start-radius: var(--wa-form-control-border-radius);\n  border-end-end-radius: var(--wa-form-control-border-radius);\n}\n\n/* Handle pill modifier for button groups */\n:host([pill].wa-button-group__horizontal.wa-button-group__button-first) .button {\n  border-start-start-radius: var(--wa-border-radius-pill);\n  border-end-start-radius: var(--wa-border-radius-pill);\n}\n\n:host([pill].wa-button-group__horizontal.wa-button-group__button-last) .button {\n  border-start-end-radius: var(--wa-border-radius-pill);\n  border-end-end-radius: var(--wa-border-radius-pill);\n}\n\n:host([pill].wa-button-group__vertical.wa-button-group__button-first) .button {\n  border-start-start-radius: var(--wa-border-radius-pill);\n  border-start-end-radius: var(--wa-border-radius-pill);\n}\n\n:host([pill].wa-button-group__vertical.wa-button-group__button-last) .button {\n  border-end-start-radius: var(--wa-border-radius-pill);\n  border-end-end-radius: var(--wa-border-radius-pill);\n}\n";

// src/components/button/button.ts
var WaButton = class extends WebAwesomeFormAssociatedElement {
  constructor() {
    super(...arguments);
    this.assumeInteractionOn = ["click"];
    this.hasSlotController = new HasSlotController(this, "[default]", "start", "end");
    this.localize = new LocalizeController(this);
    this.invalid = false;
    this.isIconButton = false;
    this.title = "";
    this.variant = "neutral";
    this.appearance = "accent";
    this.size = "medium";
    this.withCaret = false;
    this.disabled = false;
    this.loading = false;
    this.pill = false;
    this.type = "button";
    this.form = null;
  }
  static get validators() {
    return [...super.validators, MirrorValidator()];
  }
  constructLightDOMButton() {
    const button = document.createElement("button");
    button.type = this.type;
    button.style.position = "absolute";
    button.style.width = "0";
    button.style.height = "0";
    button.style.clipPath = "inset(50%)";
    button.style.overflow = "hidden";
    button.style.whiteSpace = "nowrap";
    if (this.name) {
      button.name = this.name;
    }
    button.value = this.value || "";
    ["form", "formaction", "formenctype", "formmethod", "formnovalidate", "formtarget"].forEach((attr) => {
      if (this.hasAttribute(attr)) {
        button.setAttribute(attr, this.getAttribute(attr));
      }
    });
    return button;
  }
  handleClick() {
    const form = this.getForm();
    if (!form) return;
    const lightDOMButton = this.constructLightDOMButton();
    this.parentElement?.append(lightDOMButton);
    lightDOMButton.click();
    lightDOMButton.remove();
  }
  handleInvalid() {
    this.dispatchEvent(new WaInvalidEvent());
  }
  handleLabelSlotChange() {
    const nodes = this.labelSlot.assignedNodes({ flatten: true });
    let hasIconLabel = false;
    let hasIcon = false;
    let hasText = false;
    let hasOtherElements = false;
    [...nodes].forEach((node) => {
      if (node.nodeType === Node.ELEMENT_NODE) {
        const element = node;
        if (element.localName === "wa-icon") {
          hasIcon = true;
          if (!hasIconLabel) hasIconLabel = element.label !== void 0;
        } else {
          hasOtherElements = true;
        }
      } else if (node.nodeType === Node.TEXT_NODE) {
        const text = node.textContent?.trim() || "";
        if (text.length > 0) {
          hasText = true;
        }
      }
    });
    this.isIconButton = hasIcon && !hasText && !hasOtherElements;
    if (this.isIconButton && !hasIconLabel) {
      console.warn(
        'Icon buttons must have a label for screen readers. Add <wa-icon label="..."> to remove this warning.',
        this
      );
    }
  }
  isButton() {
    return this.href ? false : true;
  }
  isLink() {
    return this.href ? true : false;
  }
  handleDisabledChange() {
    this.updateValidity();
  }
  // eslint-disable-next-line
  setValue(..._args) {
  }
  /** Simulates a click on the button. */
  click() {
    this.button.click();
  }
  /** Sets focus on the button. */
  focus(options) {
    this.button.focus(options);
  }
  /** Removes focus from the button. */
  blur() {
    this.button.blur();
  }
  render() {
    const isLink = this.isLink();
    const tag = isLink ? i`a` : i`button`;
    return u`
      <${tag}
        part="base"
        class=${e2({
      button: true,
      caret: this.withCaret,
      disabled: this.disabled,
      loading: this.loading,
      rtl: this.localize.dir() === "rtl",
      "has-label": this.hasSlotController.test("[default]"),
      "has-start": this.hasSlotController.test("start"),
      "has-end": this.hasSlotController.test("end"),
      "is-icon-button": this.isIconButton
    })}
        ?disabled=${o(isLink ? void 0 : this.disabled)}
        type=${o(isLink ? void 0 : this.type)}
        title=${this.title}
        name=${o(isLink ? void 0 : this.name)}
        value=${o(isLink ? void 0 : this.value)}
        href=${o(isLink ? this.href : void 0)}
        target=${o(isLink ? this.target : void 0)}
        download=${o(isLink ? this.download : void 0)}
        rel=${o(isLink && this.rel ? this.rel : void 0)}
        role=${o(isLink ? void 0 : "button")}
        aria-disabled=${this.disabled ? "true" : "false"}
        tabindex=${this.disabled ? "-1" : "0"}
        @invalid=${this.isButton() ? this.handleInvalid : null}
        @click=${this.handleClick}
      >
        <slot name="start" part="start" class="start"></slot>
        <slot part="label" class="label" @slotchange=${this.handleLabelSlotChange}></slot>
        <slot name="end" part="end" class="end"></slot>
        ${this.withCaret ? u`
                <wa-icon part="caret" class="caret" library="system" name="chevron-down" variant="solid"></wa-icon>
              ` : ""}
        ${this.loading ? u`<wa-spinner part="spinner"></wa-spinner>` : ""}
      </${tag}>
    `;
  }
};
WaButton.shadowRootOptions = { ...WebAwesomeFormAssociatedElement.shadowRootOptions, delegatesFocus: true };
WaButton.css = [button_default, variants_default, size_default];
__decorateClass([
  e(".button")
], WaButton.prototype, "button", 2);
__decorateClass([
  e("slot:not([name])")
], WaButton.prototype, "labelSlot", 2);
__decorateClass([
  r()
], WaButton.prototype, "invalid", 2);
__decorateClass([
  r()
], WaButton.prototype, "isIconButton", 2);
__decorateClass([
  n()
], WaButton.prototype, "title", 2);
__decorateClass([
  n({ reflect: true })
], WaButton.prototype, "variant", 2);
__decorateClass([
  n({ reflect: true })
], WaButton.prototype, "appearance", 2);
__decorateClass([
  n({ reflect: true })
], WaButton.prototype, "size", 2);
__decorateClass([
  n({ attribute: "with-caret", type: Boolean, reflect: true })
], WaButton.prototype, "withCaret", 2);
__decorateClass([
  n({ type: Boolean })
], WaButton.prototype, "disabled", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaButton.prototype, "loading", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaButton.prototype, "pill", 2);
__decorateClass([
  n()
], WaButton.prototype, "type", 2);
__decorateClass([
  n({ reflect: true })
], WaButton.prototype, "name", 2);
__decorateClass([
  n({ reflect: true })
], WaButton.prototype, "value", 2);
__decorateClass([
  n({ reflect: true })
], WaButton.prototype, "href", 2);
__decorateClass([
  n()
], WaButton.prototype, "target", 2);
__decorateClass([
  n()
], WaButton.prototype, "rel", 2);
__decorateClass([
  n()
], WaButton.prototype, "download", 2);
__decorateClass([
  n({ reflect: true })
], WaButton.prototype, "form", 2);
__decorateClass([
  n({ attribute: "formaction" })
], WaButton.prototype, "formAction", 2);
__decorateClass([
  n({ attribute: "formenctype" })
], WaButton.prototype, "formEnctype", 2);
__decorateClass([
  n({ attribute: "formmethod" })
], WaButton.prototype, "formMethod", 2);
__decorateClass([
  n({ attribute: "formnovalidate", type: Boolean })
], WaButton.prototype, "formNoValidate", 2);
__decorateClass([
  n({ attribute: "formtarget" })
], WaButton.prototype, "formTarget", 2);
__decorateClass([
  watch("disabled", { waitUntilFirstUpdate: true })
], WaButton.prototype, "handleDisabledChange", 1);
WaButton = __decorateClass([
  t("wa-button")
], WaButton);

export {
  WaButton
};
/*! Bundled license information:

lit-html/static.js:
  (**
   * @license
   * Copyright 2020 Google LLC
   * SPDX-License-Identifier: BSD-3-Clause
   *)
*/
