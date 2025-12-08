/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  drag
} from "./chunk.CI74OXU3.js";
import {
  WaRepositionEvent
} from "./chunk.AJ3PNG3J.js";
import {
  o
} from "./chunk.G3G3UFWM.js";
import {
  clamp
} from "./chunk.KABXVFSA.js";
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
  t
} from "./chunk.7F6EHFYD.js";
import {
  x
} from "./chunk.23WMFJHA.js";
import {
  __decorateClass
} from "./chunk.6E4D3PD7.js";

// src/components/split-panel/split-panel.css
var split_panel_default = ":host {\n  --divider-width: 0.25rem;\n  --divider-hit-area: 0.75rem;\n  --min: 0%;\n  --max: 100%;\n\n  display: grid;\n}\n\n.start,\n.end {\n  overflow: hidden;\n}\n\n.divider {\n  flex: 0 0 var(--divider-width);\n  display: flex;\n  position: relative;\n  align-items: center;\n  justify-content: center;\n  background-color: var(--wa-color-neutral-border-normal);\n  color: var(--wa-color-neutral-on-normal);\n  z-index: 1;\n}\n\n.divider:focus {\n  outline: none;\n}\n\n:host(:not([disabled])) .divider:focus-visible {\n  outline: var(--wa-focus-ring);\n}\n\n:host([disabled]) .divider {\n  cursor: not-allowed;\n}\n\n/* Horizontal */\n:host(:not([orientation='vertical'], [disabled])) .divider {\n  cursor: col-resize;\n}\n\n:host(:not([orientation='vertical'])) .divider::after {\n  display: flex;\n  content: '';\n  position: absolute;\n  height: 100%;\n  left: calc(var(--divider-hit-area) / -2 + var(--divider-width) / 2);\n  width: var(--divider-hit-area);\n}\n\n/* Vertical */\n:host([orientation='vertical']) {\n  flex-direction: column;\n}\n\n:host([orientation='vertical']:not([disabled])) .divider {\n  cursor: row-resize;\n}\n\n:host([orientation='vertical']) .divider::after {\n  content: '';\n  position: absolute;\n  width: 100%;\n  top: calc(var(--divider-hit-area) / -2 + var(--divider-width) / 2);\n  height: var(--divider-hit-area);\n}\n\n@media (forced-colors: active) {\n  .divider {\n    outline: solid 1px transparent;\n  }\n}\n";

// src/components/split-panel/split-panel.ts
var WaSplitPanel = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.isCollapsed = false;
    this.localize = new LocalizeController(this);
    this.positionBeforeCollapsing = 0;
    this.position = 50;
    this.orientation = "horizontal";
    this.disabled = false;
    this.snapThreshold = 12;
  }
  connectedCallback() {
    super.connectedCallback();
    this.resizeObserver = new ResizeObserver((entries) => this.handleResize(entries));
    this.updateComplete.then(() => this.resizeObserver.observe(this));
    this.detectSize();
    this.cachedPositionInPixels = this.percentageToPixels(this.position);
  }
  disconnectedCallback() {
    super.disconnectedCallback();
    this.resizeObserver?.unobserve(this);
  }
  detectSize() {
    const { width, height } = this.getBoundingClientRect();
    this.size = this.orientation === "vertical" ? height : width;
  }
  percentageToPixels(value) {
    return this.size * (value / 100);
  }
  pixelsToPercentage(value) {
    return value / this.size * 100;
  }
  handleDrag(event) {
    const isRtl = this.hasUpdated ? this.localize.dir() === "rtl" : this.dir === "rtl";
    if (this.disabled) {
      return;
    }
    if (event.cancelable) {
      event.preventDefault();
    }
    drag(this, {
      onMove: (x2, y) => {
        let newPositionInPixels = this.orientation === "vertical" ? y : x2;
        if (this.primary === "end") {
          newPositionInPixels = this.size - newPositionInPixels;
        }
        if (this.snap) {
          const snaps = this.snap.split(" ");
          snaps.forEach((value) => {
            let snapPoint;
            if (value.endsWith("%")) {
              snapPoint = this.size * (parseFloat(value) / 100);
            } else {
              snapPoint = parseFloat(value);
            }
            if (isRtl && this.orientation === "horizontal") {
              snapPoint = this.size - snapPoint;
            }
            if (newPositionInPixels >= snapPoint - this.snapThreshold && newPositionInPixels <= snapPoint + this.snapThreshold) {
              newPositionInPixels = snapPoint;
            }
          });
        }
        this.position = clamp(this.pixelsToPercentage(newPositionInPixels), 0, 100);
      },
      initialEvent: event
    });
  }
  handleKeyDown(event) {
    if (this.disabled) {
      return;
    }
    if (["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "Home", "End", "Enter"].includes(event.key)) {
      let newPosition = this.position;
      const incr = (event.shiftKey ? 10 : 1) * (this.primary === "end" ? -1 : 1);
      event.preventDefault();
      if (event.key === "ArrowLeft" && this.orientation === "horizontal" || event.key === "ArrowUp" && this.orientation === "vertical") {
        newPosition -= incr;
      }
      if (event.key === "ArrowRight" && this.orientation === "horizontal" || event.key === "ArrowDown" && this.orientation === "vertical") {
        newPosition += incr;
      }
      if (event.key === "Home") {
        newPosition = this.primary === "end" ? 100 : 0;
      }
      if (event.key === "End") {
        newPosition = this.primary === "end" ? 0 : 100;
      }
      if (event.key === "Enter") {
        if (this.isCollapsed) {
          newPosition = this.positionBeforeCollapsing;
          this.isCollapsed = false;
        } else {
          const positionBeforeCollapsing = this.position;
          newPosition = 0;
          requestAnimationFrame(() => {
            this.isCollapsed = true;
            this.positionBeforeCollapsing = positionBeforeCollapsing;
          });
        }
      }
      this.position = clamp(newPosition, 0, 100);
    }
  }
  handleResize(entries) {
    const { width, height } = entries[0].contentRect;
    this.size = this.orientation === "vertical" ? height : width;
    if (isNaN(this.cachedPositionInPixels) || this.position === Infinity) {
      this.cachedPositionInPixels = Number(this.getAttribute("position-in-pixels"));
      this.positionInPixels = Number(this.getAttribute("position-in-pixels"));
      this.position = this.pixelsToPercentage(this.positionInPixels);
    }
    if (this.primary) {
      this.position = this.pixelsToPercentage(this.cachedPositionInPixels);
    }
  }
  handlePositionChange() {
    this.cachedPositionInPixels = this.percentageToPixels(this.position);
    this.positionInPixels = this.percentageToPixels(this.position);
    this.isCollapsed = false;
    this.positionBeforeCollapsing = 0;
    this.dispatchEvent(new WaRepositionEvent());
  }
  handlePositionInPixelsChange() {
    this.position = this.pixelsToPercentage(this.positionInPixels);
  }
  handleVerticalChange() {
    this.detectSize();
  }
  render() {
    const gridTemplate = this.orientation === "vertical" ? "gridTemplateRows" : "gridTemplateColumns";
    const gridTemplateAlt = this.orientation === "vertical" ? "gridTemplateColumns" : "gridTemplateRows";
    const isRtl = this.hasUpdated ? this.localize.dir() === "rtl" : this.dir === "rtl";
    const primary = `
      clamp(
        0%,
        clamp(
          var(--min),
          ${this.position}% - var(--divider-width) / 2,
          var(--max)
        ),
        calc(100% - var(--divider-width))
      )
    `;
    const secondary = "auto";
    if (!this.style) {
      this.style = {};
    }
    if (this.primary === "end") {
      if (isRtl && this.orientation === "horizontal") {
        this.style[gridTemplate] = `${primary} var(--divider-width) ${secondary}`;
      } else {
        this.style[gridTemplate] = `${secondary} var(--divider-width) ${primary}`;
      }
    } else {
      if (isRtl && this.orientation === "horizontal") {
        this.style[gridTemplate] = `${secondary} var(--divider-width) ${primary}`;
      } else {
        this.style[gridTemplate] = `${primary} var(--divider-width) ${secondary}`;
      }
    }
    this.style[gridTemplateAlt] = "";
    return x`
      <slot name="start" part="panel start" class="start"></slot>

      <div
        part="divider"
        class="divider"
        tabindex=${o(this.disabled ? void 0 : "0")}
        role="separator"
        aria-valuenow=${this.position}
        aria-valuemin="0"
        aria-valuemax="100"
        aria-label=${this.localize.term("resize")}
        @keydown=${this.handleKeyDown}
        @mousedown=${this.handleDrag}
        @touchstart=${this.handleDrag}
      >
        <slot name="divider"></slot>
      </div>

      <slot name="end" part="panel end" class="end"></slot>
    `;
  }
};
WaSplitPanel.css = split_panel_default;
__decorateClass([
  e(".divider")
], WaSplitPanel.prototype, "divider", 2);
__decorateClass([
  n({ type: Number, reflect: true })
], WaSplitPanel.prototype, "position", 2);
__decorateClass([
  n({ attribute: "position-in-pixels", type: Number })
], WaSplitPanel.prototype, "positionInPixels", 2);
__decorateClass([
  n({ reflect: true })
], WaSplitPanel.prototype, "orientation", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaSplitPanel.prototype, "disabled", 2);
__decorateClass([
  n()
], WaSplitPanel.prototype, "primary", 2);
__decorateClass([
  n()
], WaSplitPanel.prototype, "snap", 2);
__decorateClass([
  n({ type: Number, attribute: "snap-threshold" })
], WaSplitPanel.prototype, "snapThreshold", 2);
__decorateClass([
  watch("position")
], WaSplitPanel.prototype, "handlePositionChange", 1);
__decorateClass([
  watch("positionInPixels")
], WaSplitPanel.prototype, "handlePositionInPixelsChange", 1);
__decorateClass([
  watch("vertical")
], WaSplitPanel.prototype, "handleVerticalChange", 1);
WaSplitPanel = __decorateClass([
  t("wa-split-panel")
], WaSplitPanel);

export {
  WaSplitPanel
};
