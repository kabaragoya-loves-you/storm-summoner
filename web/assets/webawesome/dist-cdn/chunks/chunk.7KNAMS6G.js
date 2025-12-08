/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  watch
} from "./chunk.N3AZYXKV.js";
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

// src/events/resize.ts
var WaResizeEvent = class extends Event {
  constructor(detail) {
    super("wa-resize", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};

// src/components/resize-observer/resize-observer.css
var resize_observer_default = ":host {\n  display: contents;\n}\n";

// src/components/resize-observer/resize-observer.ts
var WaResizeObserver = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.observedElements = [];
    this.disabled = false;
  }
  connectedCallback() {
    super.connectedCallback();
    this.resizeObserver = new ResizeObserver((entries) => {
      this.dispatchEvent(new WaResizeEvent({ entries }));
    });
    if (!this.disabled) {
      this.updateComplete.then(() => {
        this.startObserver();
      });
    }
  }
  disconnectedCallback() {
    super.disconnectedCallback();
    this.stopObserver();
  }
  handleSlotChange() {
    if (!this.disabled) {
      this.startObserver();
    }
  }
  startObserver() {
    const slot = this.shadowRoot.querySelector("slot");
    if (slot !== null) {
      const elements = slot.assignedElements({ flatten: true });
      this.observedElements.forEach((el) => this.resizeObserver.unobserve(el));
      this.observedElements = [];
      elements.forEach((el) => {
        this.resizeObserver.observe(el);
        this.observedElements.push(el);
      });
    }
  }
  stopObserver() {
    this.resizeObserver.disconnect();
  }
  handleDisabledChange() {
    if (this.disabled) {
      this.stopObserver();
    } else {
      this.startObserver();
    }
  }
  render() {
    return x` <slot @slotchange=${this.handleSlotChange}></slot> `;
  }
};
WaResizeObserver.css = resize_observer_default;
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaResizeObserver.prototype, "disabled", 2);
__decorateClass([
  watch("disabled", { waitUntilFirstUpdate: true })
], WaResizeObserver.prototype, "handleDisabledChange", 1);
WaResizeObserver = __decorateClass([
  t("wa-resize-observer")
], WaResizeObserver);

export {
  WaResizeObserver
};
