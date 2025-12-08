/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  LocalizeController
} from "./chunk.DR3YY3XN.js";
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

// src/components/scroller/scroller.css
var scroller_default = ":host {\n  --shadow-color: var(--wa-color-surface-default);\n  --shadow-size: 2rem;\n\n  /* private (defined dynamically) */\n  --start-shadow-opacity: 0;\n  --end-shadow-opacity: 0;\n\n  display: block;\n  position: relative;\n  max-width: 100%;\n  isolation: isolate;\n}\n\n:host([orientation='vertical']) {\n  display: flex;\n  flex-direction: column;\n  height: 100%;\n}\n\n#content {\n  z-index: 1; /* below shadows */\n  border-radius: inherit;\n  scroll-behavior: smooth;\n  scrollbar-width: thin;\n\n  /* Prevent text in mobile Safari from being larger when the container width larger than the viewport */\n  -webkit-text-size-adjust: 100%;\n\n  &:focus {\n    outline: none;\n  }\n\n  &:focus-visible {\n    outline: var(--wa-focus-ring);\n    outline-offset: var(--wa-focus-ring-offset);\n  }\n}\n\n:host([without-scrollbar]) #content {\n  scrollbar-width: none;\n}\n\n:host([orientation='horizontal']) #content {\n  overflow-x: auto;\n  overflow-y: hidden;\n}\n\n:host([orientation='vertical']) #content {\n  flex: 1 1 auto;\n  min-height: 0; /* This is crucial for flex children to respect overflow */\n  overflow-x: hidden;\n  overflow-y: auto;\n}\n\n/* Horizontal shadows */\n:host([orientation='horizontal']) {\n  #start-shadow,\n  #end-shadow {\n    position: absolute;\n    top: 0;\n    bottom: 0;\n    width: var(--shadow-size);\n    pointer-events: none;\n  }\n\n  #start-shadow {\n    opacity: var(--start-shadow-opacity);\n  }\n\n  #end-shadow {\n    opacity: var(--end-shadow-opacity);\n  }\n\n  #start-shadow {\n    &:dir(ltr) {\n      left: 0;\n      background: linear-gradient(to right, var(--shadow-color), transparent 100%);\n    }\n\n    &:dir(rtl) {\n      right: 0;\n      background: linear-gradient(to left, var(--shadow-color), transparent 100%);\n    }\n  }\n\n  #end-shadow {\n    &:dir(ltr) {\n      right: 0;\n      background: linear-gradient(to left, var(--shadow-color), transparent 100%);\n    }\n\n    &:dir(rtl) {\n      left: 0;\n      background: linear-gradient(to right, var(--shadow-color), transparent 100%);\n    }\n  }\n}\n\n/* Vertical shadows */\n:host([orientation='vertical']) {\n  #start-shadow,\n  #end-shadow {\n    position: absolute;\n    z-index: 2;\n    right: 0;\n    left: 0;\n    height: var(--shadow-size);\n    pointer-events: none;\n  }\n\n  #start-shadow {\n    opacity: var(--start-shadow-opacity);\n  }\n\n  #end-shadow {\n    opacity: var(--end-shadow-opacity);\n  }\n\n  #start-shadow {\n    top: 0;\n    background: linear-gradient(to bottom, var(--shadow-color), transparent 100%);\n  }\n\n  #end-shadow {\n    bottom: 0;\n    background: linear-gradient(to top, var(--shadow-color), transparent 100%);\n  }\n}\n";

// src/components/scroller/scroller.ts
var WaScroller = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.localize = new LocalizeController(this);
    this.resizeObserver = new ResizeObserver(() => this.updateScroll());
    this.canScroll = false;
    this.orientation = "horizontal";
    this.withoutScrollbar = false;
    this.withoutShadow = false;
  }
  connectedCallback() {
    super.connectedCallback();
    this.resizeObserver.observe(this);
  }
  disconnectedCallback() {
    super.disconnectedCallback();
    this.resizeObserver.disconnect();
  }
  handleKeyDown(event) {
    if (event.key === "Home") {
      event.preventDefault();
      this.content.scrollTo({
        left: this.orientation === "horizontal" ? 0 : void 0,
        top: this.orientation === "vertical" ? 0 : void 0
      });
    }
    if (event.key === "End") {
      event.preventDefault();
      this.content.scrollTo({
        left: this.orientation === "horizontal" ? this.content.scrollWidth : void 0,
        top: this.orientation === "vertical" ? this.content.scrollHeight : void 0
      });
    }
  }
  handleSlotChange() {
    this.updateScroll();
  }
  updateScroll() {
    if (this.orientation === "horizontal") {
      const clientWidth = Math.ceil(this.content.clientWidth);
      const scrollLeft = Math.abs(Math.ceil(this.content.scrollLeft));
      const scrollWidth = Math.ceil(this.content.scrollWidth);
      const maxScroll = scrollWidth - clientWidth;
      this.canScroll = maxScroll > 0;
      const startShadowOpacity = Math.min(1, scrollLeft / (maxScroll * 0.05));
      const endShadowOpacity = Math.min(1, (maxScroll - scrollLeft) / (maxScroll * 0.05));
      this.style.setProperty("--start-shadow-opacity", String(startShadowOpacity || 0));
      this.style.setProperty("--end-shadow-opacity", String(endShadowOpacity || 0));
    } else {
      const clientHeight = Math.ceil(this.content.clientHeight);
      const scrollTop = Math.abs(Math.ceil(this.content.scrollTop));
      const scrollHeight = Math.ceil(this.content.scrollHeight);
      const maxScroll = scrollHeight - clientHeight;
      this.canScroll = maxScroll > 0;
      const startShadowOpacity = Math.min(1, scrollTop / (maxScroll * 0.05));
      const endShadowOpacity = Math.min(1, (maxScroll - scrollTop) / (maxScroll * 0.05));
      this.style.setProperty("--start-shadow-opacity", String(startShadowOpacity || 0));
      this.style.setProperty("--end-shadow-opacity", String(endShadowOpacity || 0));
    }
  }
  render() {
    return x`
      ${this.withoutShadow ? "" : x`
            <div id="start-shadow" part="start-shadow" aria-hidden="true"></div>
            <div id="end-shadow" part="end-shadow" aria-hidden="true"></div>
          `}

      <div
        id="content"
        part="content"
        role="region"
        aria-label=${this.localize.term("scrollableRegion")}
        aria-orientation=${this.orientation}
        tabindex=${this.canScroll ? "0" : "-1"}
        @keydown=${this.handleKeyDown}
        @scroll=${this.updateScroll}
      >
        <slot @slotchange=${this.handleSlotChange}></slot>
      </div>
    `;
  }
};
WaScroller.css = [scroller_default];
__decorateClass([
  e("#content")
], WaScroller.prototype, "content", 2);
__decorateClass([
  r()
], WaScroller.prototype, "canScroll", 2);
__decorateClass([
  n({ reflect: true })
], WaScroller.prototype, "orientation", 2);
__decorateClass([
  n({ attribute: "without-scrollbar", type: Boolean, reflect: true })
], WaScroller.prototype, "withoutScrollbar", 2);
__decorateClass([
  n({ attribute: "without-shadow", type: Boolean, reflect: true })
], WaScroller.prototype, "withoutShadow", 2);
__decorateClass([
  t2({ passive: true })
], WaScroller.prototype, "updateScroll", 1);
WaScroller = __decorateClass([
  t("wa-scroller")
], WaScroller);

export {
  WaScroller
};
