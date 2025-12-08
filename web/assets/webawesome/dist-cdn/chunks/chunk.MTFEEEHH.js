/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  o as o2
} from "./chunk.PQKSMT7C.js";
import {
  o
} from "./chunk.4H6H3F5U.js";
import {
  size_default
} from "./chunk.GY3LNU3J.js";
import {
  clamp
} from "./chunk.KABXVFSA.js";
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
  t,
  t2
} from "./chunk.7F6EHFYD.js";
import {
  x
} from "./chunk.23WMFJHA.js";
import {
  __decorateClass
} from "./chunk.6E4D3PD7.js";

// src/events/hover.ts
var WaHoverEvent = class extends Event {
  constructor(detail) {
    super("wa-hover", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};

// src/components/rating/rating.css
var rating_default = ":host {\n  --symbol-color: var(--wa-color-neutral-on-quiet);\n  --symbol-color-active: var(--wa-color-yellow-70);\n  --symbol-spacing: 0.125em;\n\n  display: inline-flex;\n}\n\n.rating {\n  position: relative;\n  display: inline-flex;\n  border-radius: var(--wa-border-radius-m);\n  vertical-align: middle;\n}\n\n.rating:focus {\n  outline: none;\n}\n\n.rating:focus-visible {\n  outline: var(--wa-focus-ring);\n  outline-offset: var(--wa-focus-ring-offset);\n}\n\n.symbols {\n  display: inline-flex;\n  gap: 0.125em;\n  position: relative;\n  line-height: 0;\n  color: var(--symbol-color);\n  white-space: nowrap;\n  cursor: pointer;\n}\n\n.symbols > * {\n  padding: var(--symbol-spacing);\n}\n\n.symbol-active,\n.partial-filled {\n  color: var(--symbol-color-active);\n}\n\n.partial-symbol-container {\n  position: relative;\n}\n\n.partial-filled {\n  position: absolute;\n  top: var(--symbol-spacing);\n  left: var(--symbol-spacing);\n}\n\n.symbol {\n  transition: scale var(--wa-transition-normal) var(--wa-transition-easing);\n  pointer-events: none;\n}\n\n.symbol-hover {\n  scale: 1.2;\n}\n\n.rating-readonly .symbols {\n  cursor: default;\n}\n\n:host([disabled]) .symbol-hover,\n.rating-readonly .symbol-hover {\n  scale: none;\n}\n\n:host([disabled]) {\n  opacity: 0.5;\n}\n\n:host([disabled]) .symbols {\n  cursor: not-allowed;\n}\n\n/* Forced colors mode */\n@media (forced-colors: active) {\n  .symbol-active {\n    color: SelectedItem;\n  }\n}\n";

// src/components/rating/rating.ts
var WaRating = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.localize = new LocalizeController(this);
    this.hoverValue = 0;
    this.isHovering = false;
    this.label = "";
    this.value = 0;
    this.max = 5;
    this.precision = 1;
    this.readonly = false;
    this.disabled = false;
    this.getSymbol = (_value, isSelected) => {
      return isSelected ? '<wa-icon name="star" library="system" variant="solid"></wa-icon>' : '<wa-icon name="star" library="system" variant="regular"></wa-icon>';
    };
    this.size = "medium";
  }
  getValueFromMousePosition(event) {
    return this.getValueFromXCoordinate(event.clientX);
  }
  getValueFromTouchPosition(event) {
    return this.getValueFromXCoordinate(event.touches[0].clientX);
  }
  getValueFromXCoordinate(coordinate) {
    const isRtl = this.localize.dir() === "rtl";
    const { left, right, width } = this.rating.getBoundingClientRect();
    const value = isRtl ? this.roundToPrecision((right - coordinate) / width * this.max, this.precision) : this.roundToPrecision((coordinate - left) / width * this.max, this.precision);
    return clamp(value, 0, this.max);
  }
  handleClick(event) {
    if (this.disabled) {
      return;
    }
    this.setValue(this.getValueFromMousePosition(event));
    this.updateComplete.then(() => {
      this.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
    });
  }
  setValue(newValue) {
    if (this.disabled || this.readonly) {
      return;
    }
    this.value = newValue === this.value ? 0 : newValue;
    this.isHovering = false;
  }
  handleKeyDown(event) {
    const isLtr = this.matches(":dir(ltr)");
    const isRtl = this.localize.dir() === "rtl";
    const oldValue = this.value;
    if (this.disabled || this.readonly) {
      return;
    }
    if (event.key === "ArrowDown" || isLtr && event.key === "ArrowLeft" || isRtl && event.key === "ArrowRight") {
      const decrement = event.shiftKey ? 1 : this.precision;
      this.value = Math.max(0, this.value - decrement);
      event.preventDefault();
    }
    if (event.key === "ArrowUp" || isLtr && event.key === "ArrowRight" || isRtl && event.key === "ArrowLeft") {
      const increment = event.shiftKey ? 1 : this.precision;
      this.value = Math.min(this.max, this.value + increment);
      event.preventDefault();
    }
    if (event.key === "Home") {
      this.value = 0;
      event.preventDefault();
    }
    if (event.key === "End") {
      this.value = this.max;
      event.preventDefault();
    }
    if (this.value !== oldValue) {
      this.updateComplete.then(() => {
        this.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
      });
    }
  }
  handleMouseEnter(event) {
    this.isHovering = true;
    this.hoverValue = this.getValueFromMousePosition(event);
  }
  handleMouseMove(event) {
    this.hoverValue = this.getValueFromMousePosition(event);
  }
  handleMouseLeave() {
    this.isHovering = false;
  }
  handleTouchStart(event) {
    this.isHovering = true;
    this.hoverValue = this.getValueFromTouchPosition(event);
    event.preventDefault();
  }
  handleTouchMove(event) {
    this.hoverValue = this.getValueFromTouchPosition(event);
  }
  handleTouchEnd(event) {
    this.isHovering = false;
    this.setValue(this.hoverValue);
    this.updateComplete.then(() => {
      this.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
    });
    event.preventDefault();
  }
  roundToPrecision(numberToRound, precision = 0.5) {
    const multiplier = 1 / precision;
    return Math.ceil(numberToRound * multiplier) / multiplier;
  }
  handleHoverValueChange() {
    this.dispatchEvent(
      new WaHoverEvent({
        phase: "move",
        value: this.hoverValue
      })
    );
  }
  handleIsHoveringChange() {
    this.dispatchEvent(
      new WaHoverEvent({
        phase: this.isHovering ? "start" : "end",
        value: this.hoverValue
      })
    );
  }
  /** Sets focus on the rating. */
  focus(options) {
    this.rating.focus(options);
  }
  /** Removes focus from the rating. */
  blur() {
    this.rating.blur();
  }
  render() {
    const isRtl = this.hasUpdated ? this.localize.dir() === "rtl" : this.dir;
    const counter = Array.from(Array(this.max).keys());
    let displayValue = 0;
    if (this.disabled || this.readonly) {
      displayValue = this.value;
    } else {
      displayValue = this.isHovering ? this.hoverValue : this.value;
    }
    return x`
      <div
        part="base"
        class=${e2({
      rating: true,
      "rating-readonly": this.readonly,
      "rating-disabled": this.disabled
    })}
        role="slider"
        aria-label=${this.label}
        aria-disabled=${this.disabled ? "true" : "false"}
        aria-readonly=${this.readonly ? "true" : "false"}
        aria-valuenow=${this.value}
        aria-valuemin=${0}
        aria-valuemax=${this.max}
        tabindex=${this.disabled || this.readonly ? "-1" : "0"}
        @click=${this.handleClick}
        @keydown=${this.handleKeyDown}
        @mouseenter=${this.handleMouseEnter}
        @touchstart=${this.handleTouchStart}
        @mouseleave=${this.handleMouseLeave}
        @touchend=${this.handleTouchEnd}
        @mousemove=${this.handleMouseMove}
        @touchmove=${this.handleTouchMove}
      >
        <span class="symbols">
          ${counter.map((index) => {
      const isSelected = displayValue >= index + 1;
      if (displayValue > index && displayValue < index + 1) {
        return x`
                <span
                  class=${e2({
          symbol: true,
          "partial-symbol-container": true,
          "symbol-hover": this.isHovering && Math.ceil(displayValue) === index + 1
        })}
                  role="presentation"
                >
                  <div
                    style=${o({
          clipPath: isRtl ? `inset(0 ${(displayValue - index) * 100}% 0 0)` : `inset(0 0 0 ${(displayValue - index) * 100}%)`
        })}
                  >
                    ${o2(this.getSymbol(index + 1, false))}
                  </div>
                  <div
                    class="partial-filled"
                    style=${o({
          clipPath: isRtl ? `inset(0 0 0 ${100 - (displayValue - index) * 100}%)` : `inset(0 ${100 - (displayValue - index) * 100}% 0 0)`
        })}
                  >
                    ${o2(this.getSymbol(index + 1, true))}
                  </div>
                </span>
              `;
      }
      return x`
              <span
                class=${e2({
        symbol: true,
        "symbol-hover": this.isHovering && Math.ceil(displayValue) === index + 1,
        "symbol-active": displayValue >= index + 1
      })}
                role="presentation"
              >
                ${o2(this.getSymbol(index + 1, isSelected))}
              </span>
            `;
    })}
        </span>
      </div>
    `;
  }
};
WaRating.css = [size_default, rating_default];
__decorateClass([
  e(".rating")
], WaRating.prototype, "rating", 2);
__decorateClass([
  r()
], WaRating.prototype, "hoverValue", 2);
__decorateClass([
  r()
], WaRating.prototype, "isHovering", 2);
__decorateClass([
  n()
], WaRating.prototype, "label", 2);
__decorateClass([
  n({ type: Number })
], WaRating.prototype, "value", 2);
__decorateClass([
  n({ type: Number })
], WaRating.prototype, "max", 2);
__decorateClass([
  n({ type: Number })
], WaRating.prototype, "precision", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaRating.prototype, "readonly", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaRating.prototype, "disabled", 2);
__decorateClass([
  n()
], WaRating.prototype, "getSymbol", 2);
__decorateClass([
  n({ reflect: true })
], WaRating.prototype, "size", 2);
__decorateClass([
  t2({ passive: true })
], WaRating.prototype, "handleTouchMove", 1);
__decorateClass([
  watch("hoverValue")
], WaRating.prototype, "handleHoverValueChange", 1);
__decorateClass([
  watch("isHovering")
], WaRating.prototype, "handleIsHoveringChange", 1);
WaRating = __decorateClass([
  t("wa-rating")
], WaRating);

export {
  WaRating
};
