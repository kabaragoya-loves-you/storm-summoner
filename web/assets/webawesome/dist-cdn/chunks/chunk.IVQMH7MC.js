/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  HasSlotController
} from "./chunk.SFADIYDM.js";
import {
  size_default
} from "./chunk.GY3LNU3J.js";
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

// src/components/card/card.css
var card_default = ":host {\n  --spacing: var(--wa-space-l);\n\n  /* Internal calculated properties */\n  --inner-border-radius: calc(var(--wa-panel-border-radius) - var(--wa-panel-border-width));\n\n  display: flex;\n  flex-direction: column;\n  background-color: var(--wa-color-surface-default);\n  border-color: var(--wa-color-surface-border);\n  border-radius: var(--wa-panel-border-radius);\n  border-style: var(--wa-panel-border-style);\n  box-shadow: var(--wa-shadow-s);\n  border-width: var(--wa-panel-border-width);\n  color: var(--wa-color-text-normal);\n}\n\n/* Appearance modifiers */\n:host([appearance~='plain']) {\n  background-color: transparent;\n  border-color: transparent;\n  box-shadow: none;\n}\n\n:host([appearance~='outlined']) {\n  background-color: var(--wa-color-surface-default);\n  border-color: var(--wa-color-surface-border);\n}\n\n:host([appearance~='filled']) {\n  background-color: var(--wa-color-neutral-fill-quiet);\n  border-color: transparent;\n}\n\n:host([appearance~='filled'][appearance~='outlined']) {\n  border-color: var(--wa-color-neutral-border-quiet);\n}\n\n:host([appearance~='accent']) {\n  color: var(--wa-color-neutral-on-loud);\n  background-color: var(--wa-color-neutral-fill-loud);\n  border-color: transparent;\n}\n\n/* Take care of top and bottom radii */\n.media,\n:host(:not([with-media])) .header,\n:host(:not([with-media], [with-header])) .body {\n  border-start-start-radius: var(--inner-border-radius);\n  border-start-end-radius: var(--inner-border-radius);\n}\n\n:host(:not([with-footer])) .body,\n.footer {\n  border-end-start-radius: var(--inner-border-radius);\n  border-end-end-radius: var(--inner-border-radius);\n}\n\n.media {\n  display: flex;\n  overflow: hidden;\n\n  &::slotted(*) {\n    display: block;\n    width: 100%;\n    border-radius: 0 !important;\n  }\n}\n\n/* Round all corners for plain appearance */\n:host([appearance='plain']) .media {\n  border-radius: var(--inner-border-radius);\n\n  &::slotted(*) {\n    border-radius: inherit !important;\n  }\n}\n\n.header {\n  display: block;\n  border-block-end-style: inherit;\n  border-block-end-color: var(--wa-color-surface-border);\n  border-block-end-width: var(--wa-panel-border-width);\n  padding: calc(var(--spacing) / 2) var(--spacing);\n}\n\n.body {\n  display: block;\n  padding: var(--spacing);\n}\n\n.footer {\n  display: block;\n  border-block-start-style: inherit;\n  border-block-start-color: var(--wa-color-surface-border);\n  border-block-start-width: var(--wa-panel-border-width);\n  padding: var(--spacing);\n}\n\n/* Push slots to sides when the action slots renders */\n.has-actions {\n  display: flex;\n  align-items: center;\n  justify-content: space-between;\n}\n\n:host(:not([with-header])) .header,\n:host(:not([with-footer])) .footer,\n:host(:not([with-media])) .media {\n  display: none;\n}\n\n/* Orientation Styles */\n:host([orientation='horizontal']) {\n  flex-direction: row;\n\n  .media {\n    border-start-start-radius: var(--inner-border-radius);\n    border-end-start-radius: var(--inner-border-radius);\n    border-start-end-radius: 0;\n\n    &::slotted(*) {\n      block-size: 100%;\n      inline-size: 100%;\n      object-fit: cover;\n    }\n  }\n}\n\n:host([orientation='horizontal']) ::slotted([slot='body']) {\n  display: block;\n  height: 100%;\n  margin: 0;\n}\n\n:host([orientation='horizontal']) ::slotted([slot='actions']) {\n  display: flex;\n  align-items: center;\n  padding: var(--spacing);\n}\n";

// src/components/card/card.ts
var WaCard = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.hasSlotController = new HasSlotController(this, "footer", "header", "media");
    this.appearance = "outlined";
    this.withHeader = false;
    this.withMedia = false;
    this.withFooter = false;
    this.orientation = "vertical";
  }
  updated() {
    if (!this.withHeader && this.hasSlotController.test("header")) this.withHeader = true;
    if (!this.withMedia && this.hasSlotController.test("media")) this.withMedia = true;
    if (!this.withFooter && this.hasSlotController.test("footer")) this.withFooter = true;
  }
  render() {
    if (this.orientation === "horizontal") {
      return x`
        <slot name="media" part="media" class="media"></slot>
        <slot part="body" class="body"></slot>
        <slot name="actions" part="actions" class="actions"></slot>
      `;
    }
    return x`
      <slot name="media" part="media" class="media"></slot>

      ${this.hasSlotController.test("header-actions") ? x` <header part="header" class="header has-actions">
            <slot name="header"></slot>
            <slot name="header-actions"></slot>
          </header>` : x` <header part="header" class="header">
            <slot name="header"></slot>
          </header>`}

      <slot part="body" class="body"></slot>
      ${this.hasSlotController.test("footer-actions") ? x` <footer part="footer" class="footer has-actions">
            <slot name="footer"></slot>
            <slot name="footer-actions"></slot>
          </footer>` : x` <footer part="footer" class="footer">
            <slot name="footer"></slot>
          </footer>`}
    `;
  }
};
WaCard.css = [size_default, card_default];
__decorateClass([
  n({ reflect: true })
], WaCard.prototype, "appearance", 2);
__decorateClass([
  n({ attribute: "with-header", type: Boolean, reflect: true })
], WaCard.prototype, "withHeader", 2);
__decorateClass([
  n({ attribute: "with-media", type: Boolean, reflect: true })
], WaCard.prototype, "withMedia", 2);
__decorateClass([
  n({ attribute: "with-footer", type: Boolean, reflect: true })
], WaCard.prototype, "withFooter", 2);
__decorateClass([
  n({ reflect: true })
], WaCard.prototype, "orientation", 2);
WaCard = __decorateClass([
  t("wa-card")
], WaCard);

export {
  WaCard
};
