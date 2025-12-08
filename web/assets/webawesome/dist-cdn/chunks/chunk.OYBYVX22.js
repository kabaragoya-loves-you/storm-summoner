/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  o
} from "./chunk.4H6H3F5U.js";
import {
  drag
} from "./chunk.CI74OXU3.js";
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

// src/components/comparison/comparison.css
var comparison_default = ":host {\n  --divider-width: 0.125rem;\n  --handle-size: 2.5rem;\n\n  display: block;\n  position: relative;\n  max-width: 100%;\n  max-height: 100%;\n  overflow: hidden;\n}\n\n.before,\n.after {\n  display: block;\n\n  &::slotted(img),\n  &::slotted(svg) {\n    display: block;\n    max-width: 100% !important;\n    height: auto;\n  }\n\n  &::slotted(:not(img, svg)) {\n    isolation: isolate;\n  }\n}\n\n.after {\n  position: absolute;\n  top: 0;\n  left: 0;\n  height: 100%;\n  width: 100%;\n}\n\n/* Disable pointer-events while dragging. This is especially important for iframes. */\n:host(:state(dragging)) {\n  .before,\n  .after {\n    pointer-events: none;\n  }\n}\n\n.divider {\n  display: flex;\n  align-items: center;\n  justify-content: center;\n  position: absolute;\n  top: 0;\n  width: var(--divider-width);\n  height: 100%;\n  background-color: var(--wa-color-surface-default);\n  translate: calc(var(--divider-width) / -2);\n  cursor: ew-resize;\n}\n\n.handle {\n  display: flex;\n  align-items: center;\n  justify-content: center;\n  position: absolute;\n  top: calc(50% - (var(--handle-size) / 2));\n  width: var(--handle-size);\n  height: var(--handle-size);\n  background-color: var(--wa-color-surface-default);\n  border-radius: var(--wa-border-radius-circle);\n  font-size: calc(var(--handle-size) * 0.4);\n  color: var(--wa-color-neutral-on-quiet);\n  cursor: inherit;\n  z-index: 10;\n}\n\n.handle:focus-visible {\n  outline: var(--wa-focus-ring);\n  outline-offset: var(--wa-focus-ring-offset);\n}\n";

// src/components/comparison/comparison.ts
var WaComparison = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.localize = new LocalizeController(this);
    this.position = 50;
  }
  handleDrag(event) {
    const { width } = this.getBoundingClientRect();
    const isRtl = this.localize.dir() === "rtl";
    event.preventDefault();
    drag(this, {
      onMove: (x2) => {
        this.customStates.set("dragging", true);
        this.position = parseFloat(clamp(x2 / width * 100, 0, 100).toFixed(2));
        if (isRtl) this.position = 100 - this.position;
      },
      onStop: () => {
        this.customStates.set("dragging", false);
      },
      initialEvent: event
    });
  }
  handleKeyDown(event) {
    const isLtr = this.matches(":dir(ltr)");
    const isRtl = this.localize.dir() === "rtl";
    if (["ArrowLeft", "ArrowRight", "Home", "End"].includes(event.key)) {
      const incr = event.shiftKey ? 10 : 1;
      let newPosition = this.position;
      event.preventDefault();
      if (isLtr && event.key === "ArrowLeft" || isRtl && event.key === "ArrowRight") {
        newPosition -= incr;
      }
      if (isLtr && event.key === "ArrowRight" || isRtl && event.key === "ArrowLeft") {
        newPosition += incr;
      }
      if (event.key === "Home") {
        newPosition = 0;
      }
      if (event.key === "End") {
        newPosition = 100;
      }
      newPosition = clamp(newPosition, 0, 100);
      this.position = newPosition;
    }
  }
  handlePositionChange() {
    this.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
  }
  render() {
    const isRtl = this.hasUpdated ? this.localize.dir() === "rtl" : this.dir === "rtl";
    return x`
      <div id="comparison" class="image" part="base">
        <div part="before" class="before">
          <slot name="before"></slot>
        </div>

        <div
          part="after"
          class="after"
          style=${o({
      clipPath: isRtl ? `inset(0 0 0 ${100 - this.position}%)` : `inset(0 ${100 - this.position}% 0 0)`
    })}
        >
          <slot name="after"></slot>
        </div>
      </div>

      <div
        part="divider"
        class="divider"
        style=${o({
      left: isRtl ? `${100 - this.position}%` : `${this.position}%`
    })}
        @keydown=${this.handleKeyDown}
        @mousedown=${this.handleDrag}
        @touchstart=${this.handleDrag}
      >
        <div
          part="handle"
          class="handle"
          role="scrollbar"
          aria-valuenow=${this.position}
          aria-valuemin="0"
          aria-valuemax="100"
          aria-controls="comparison"
          tabindex="0"
        >
          <slot name="handle">
            <wa-icon library="system" name="grip-vertical" variant="solid"></wa-icon>
          </slot>
        </div>
      </div>
    `;
  }
};
WaComparison.css = comparison_default;
__decorateClass([
  e(".handle")
], WaComparison.prototype, "handle", 2);
__decorateClass([
  n({ type: Number, reflect: true })
], WaComparison.prototype, "position", 2);
__decorateClass([
  watch("position", { waitUntilFirstUpdate: true })
], WaComparison.prototype, "handlePositionChange", 1);
WaComparison = __decorateClass([
  t("wa-comparison")
], WaComparison);

export {
  WaComparison
};
