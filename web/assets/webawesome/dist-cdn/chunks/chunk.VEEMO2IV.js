/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  variants_default
} from "./chunk.R6O3GHMM.js";
import {
  size_default
} from "./chunk.GY3LNU3J.js";
import {
  LocalizeController
} from "./chunk.DR3YY3XN.js";
import {
  WebAwesomeElement,
  n,
  t
} from "./chunk.7F6EHFYD.js";
import {
  x
} from "./chunk.23WMFJHA.js";
import {
  __decorateClass
} from "./chunk.6E4D3PD7.js";

// src/events/remove.ts
var WaRemoveEvent = class extends Event {
  constructor() {
    super("wa-remove", { bubbles: true, cancelable: false, composed: true });
  }
};

// src/components/tag/tag.css
var tag_default = "@layer wa-component {\n  :host {\n    display: inline-flex;\n    gap: 0.5em;\n    border-radius: var(--wa-border-radius-m);\n    align-items: center;\n    background-color: var(--wa-color-fill-quiet, var(--wa-color-neutral-fill-quiet));\n    border-color: var(--wa-color-border-normal, var(--wa-color-neutral-border-normal));\n    border-style: var(--wa-border-style);\n    border-width: var(--wa-border-width-s);\n    color: var(--wa-color-on-quiet, var(--wa-color-neutral-on-quiet));\n    font-size: inherit;\n    line-height: 1;\n    white-space: nowrap;\n    user-select: none;\n    -webkit-user-select: none;\n    height: calc(var(--wa-form-control-height) * 0.8);\n    line-height: calc(var(--wa-form-control-height) - var(--wa-form-control-border-width) * 2);\n    padding: 0 0.75em;\n  }\n\n  /* Appearance modifiers */\n  :host([appearance='outlined']) {\n    color: var(--wa-color-on-quiet, var(--wa-color-neutral-on-quiet));\n    background-color: transparent;\n    border-color: var(--wa-color-border-loud, var(--wa-color-neutral-border-loud));\n  }\n\n  :host([appearance='filled']) {\n    color: var(--wa-color-on-quiet, var(--wa-color-neutral-on-quiet));\n    background-color: var(--wa-color-fill-quiet, var(--wa-color-neutral-fill-quiet));\n    border-color: transparent;\n  }\n\n  :host([appearance='filled-outlined']) {\n    color: var(--wa-color-on-quiet, var(--wa-color-neutral-on-quiet));\n    background-color: var(--wa-color-fill-quiet, var(--wa-color-neutral-fill-quiet));\n    border-color: var(--wa-color-border-normal, var(--wa-color-neutral-border-normal));\n  }\n\n  :host([appearance='accent']) {\n    color: var(--wa-color-on-loud, var(--wa-color-neutral-on-loud));\n    background-color: var(--wa-color-fill-loud, var(--wa-color-neutral-fill-loud));\n    border-color: transparent;\n  }\n}\n\n.content {\n  font-size: var(--wa-font-size-smaller);\n}\n\n[part='remove-button'] {\n  color: inherit;\n  line-height: 1;\n}\n\n[part='remove-button']::part(base) {\n  padding: 0;\n  height: 1em;\n  width: 1em;\n}\n\n@media (hover: hover) {\n  :host(:hover) > [part='remove-button']::part(base) {\n    color: color-mix(in oklab, currentColor, var(--wa-color-mix-hover));\n  }\n}\n\n:host(:active) > [part='remove-button']::part(base) {\n  color: color-mix(in oklab, currentColor, var(--wa-color-mix-active));\n}\n\n/*\n * Pill modifier\n */\n:host([pill]) {\n  border-radius: var(--wa-border-radius-pill);\n}\n";

// src/components/tag/tag.ts
var WaTag = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.localize = new LocalizeController(this);
    this.variant = "neutral";
    this.appearance = "filled-outlined";
    this.size = "medium";
    this.pill = false;
    this.withRemove = false;
  }
  handleRemoveClick() {
    this.dispatchEvent(new WaRemoveEvent());
  }
  render() {
    return x`
      <slot part="content" class="content"></slot>

      ${this.withRemove ? x`
            <wa-button
              part="remove-button"
              exportparts="base:remove-button__base"
              class="remove"
              appearance="plain"
              @click=${this.handleRemoveClick}
              tabindex="-1"
            >
              <wa-icon name="xmark" library="system" variant="solid" label=${this.localize.term("remove")}></wa-icon>
            </wa-button>
          ` : ""}
    `;
  }
};
WaTag.css = [tag_default, variants_default, size_default];
__decorateClass([
  n({ reflect: true })
], WaTag.prototype, "variant", 2);
__decorateClass([
  n({ reflect: true })
], WaTag.prototype, "appearance", 2);
__decorateClass([
  n({ reflect: true })
], WaTag.prototype, "size", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaTag.prototype, "pill", 2);
__decorateClass([
  n({ attribute: "with-remove", type: Boolean })
], WaTag.prototype, "withRemove", 2);
WaTag = __decorateClass([
  t("wa-tag")
], WaTag);

export {
  WaTag
};
