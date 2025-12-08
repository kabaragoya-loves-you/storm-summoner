/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  WaLoadEvent
} from "./chunk.YNHANM3W.js";
import {
  e
} from "./chunk.DZXNOXRU.js";
import {
  getDefaultIconFamily,
  getIconLibrary,
  unwatchIcon,
  watchIcon
} from "./chunk.LTSJC6DR.js";
import {
  watch
} from "./chunk.N3AZYXKV.js";
import {
  WebAwesomeElement,
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

// src/events/error.ts
var WaErrorEvent = class extends Event {
  constructor() {
    super("wa-error", { bubbles: true, cancelable: false, composed: true });
  }
};

// src/components/icon/icon.css
var icon_default = ":host {\n  --primary-color: currentColor;\n  --primary-opacity: 1;\n  --secondary-color: currentColor;\n  --secondary-opacity: 0.4;\n\n  box-sizing: content-box;\n  display: inline-flex;\n  align-items: center;\n  justify-content: center;\n  vertical-align: -0.125em;\n}\n\n/* Standard */\n:host(:not([auto-width])) {\n  width: 1.25em;\n  height: 1em;\n}\n\n/* Auto-width */\n:host([auto-width]) {\n  width: auto;\n  height: 1em;\n}\n\nsvg {\n  height: 1em;\n  fill: currentColor;\n  overflow: visible;\n\n  /* Duotone colors with path-specific opacity fallback */\n  path[data-duotone-primary] {\n    color: var(--primary-color);\n    opacity: var(--path-opacity, var(--primary-opacity));\n  }\n\n  path[data-duotone-secondary] {\n    color: var(--secondary-color);\n    opacity: var(--path-opacity, var(--secondary-opacity));\n  }\n}\n";

// src/components/icon/icon.ts
var CACHEABLE_ERROR = Symbol();
var RETRYABLE_ERROR = Symbol();
var parser;
var iconCache = /* @__PURE__ */ new Map();
var WaIcon = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.svg = null;
    this.autoWidth = false;
    this.swapOpacity = false;
    this.label = "";
    this.library = "default";
    /** Given a URL, this function returns the resulting SVG element or an appropriate error symbol. */
    this.resolveIcon = async (url, library) => {
      let fileData;
      if (library?.spriteSheet) {
        if (!this.hasUpdated) {
          await this.updateComplete;
        }
        this.svg = x`<svg part="svg">
        <use part="use" href="${url}"></use>
      </svg>`;
        await this.updateComplete;
        const svg = this.shadowRoot.querySelector("[part='svg']");
        if (typeof library.mutator === "function") {
          library.mutator(svg, this);
        }
        return this.svg;
      }
      try {
        fileData = await fetch(url, { mode: "cors" });
        if (!fileData.ok) return fileData.status === 410 ? CACHEABLE_ERROR : RETRYABLE_ERROR;
      } catch {
        return RETRYABLE_ERROR;
      }
      try {
        const div = document.createElement("div");
        div.innerHTML = await fileData.text();
        const svg = div.firstElementChild;
        if (svg?.tagName?.toLowerCase() !== "svg") return CACHEABLE_ERROR;
        if (!parser) parser = new DOMParser();
        const doc = parser.parseFromString(svg.outerHTML, "text/html");
        const svgEl = doc.body.querySelector("svg");
        if (!svgEl) return CACHEABLE_ERROR;
        svgEl.part.add("svg");
        return document.adoptNode(svgEl);
      } catch {
        return CACHEABLE_ERROR;
      }
    };
  }
  connectedCallback() {
    super.connectedCallback();
    watchIcon(this);
  }
  firstUpdated(changedProperties) {
    super.firstUpdated(changedProperties);
    this.setIcon();
  }
  disconnectedCallback() {
    super.disconnectedCallback();
    unwatchIcon(this);
  }
  getIconSource() {
    const library = getIconLibrary(this.library);
    const family = this.family || getDefaultIconFamily();
    if (this.name && library) {
      return {
        url: library.resolver(this.name, family, this.variant, this.autoWidth),
        fromLibrary: true
      };
    }
    return {
      url: this.src,
      fromLibrary: false
    };
  }
  handleLabelChange() {
    const hasLabel = typeof this.label === "string" && this.label.length > 0;
    if (hasLabel) {
      this.setAttribute("role", "img");
      this.setAttribute("aria-label", this.label);
      this.removeAttribute("aria-hidden");
    } else {
      this.removeAttribute("role");
      this.removeAttribute("aria-label");
      this.setAttribute("aria-hidden", "true");
    }
  }
  async setIcon() {
    const { url, fromLibrary } = this.getIconSource();
    const library = fromLibrary ? getIconLibrary(this.library) : void 0;
    if (!url) {
      this.svg = null;
      return;
    }
    let iconResolver = iconCache.get(url);
    if (!iconResolver) {
      iconResolver = this.resolveIcon(url, library);
      iconCache.set(url, iconResolver);
    }
    const svg = await iconResolver;
    if (svg === RETRYABLE_ERROR) {
      iconCache.delete(url);
    }
    if (url !== this.getIconSource().url) {
      return;
    }
    if (e(svg)) {
      this.svg = svg;
      return;
    }
    switch (svg) {
      case RETRYABLE_ERROR:
      case CACHEABLE_ERROR:
        this.svg = null;
        this.dispatchEvent(new WaErrorEvent());
        break;
      default:
        this.svg = svg.cloneNode(true);
        library?.mutator?.(this.svg, this);
        this.dispatchEvent(new WaLoadEvent());
    }
  }
  updated(changedProperties) {
    super.updated(changedProperties);
    const library = getIconLibrary(this.library);
    const svg = this.shadowRoot?.querySelector("svg");
    if (svg) {
      library?.mutator?.(svg, this);
    }
  }
  render() {
    if (this.hasUpdated) {
      return this.svg;
    }
    return x`<svg part="svg" fill="currentColor" width="16" height="16"></svg>`;
  }
};
WaIcon.css = icon_default;
__decorateClass([
  r()
], WaIcon.prototype, "svg", 2);
__decorateClass([
  n({ reflect: true })
], WaIcon.prototype, "name", 2);
__decorateClass([
  n({ reflect: true })
], WaIcon.prototype, "family", 2);
__decorateClass([
  n({ reflect: true })
], WaIcon.prototype, "variant", 2);
__decorateClass([
  n({ attribute: "auto-width", type: Boolean, reflect: true })
], WaIcon.prototype, "autoWidth", 2);
__decorateClass([
  n({ attribute: "swap-opacity", type: Boolean, reflect: true })
], WaIcon.prototype, "swapOpacity", 2);
__decorateClass([
  n()
], WaIcon.prototype, "src", 2);
__decorateClass([
  n()
], WaIcon.prototype, "label", 2);
__decorateClass([
  n({ reflect: true })
], WaIcon.prototype, "library", 2);
__decorateClass([
  watch("label")
], WaIcon.prototype, "handleLabelChange", 1);
__decorateClass([
  watch(["family", "name", "library", "variant", "src", "autoWidth", "swapOpacity"])
], WaIcon.prototype, "setIcon", 1);
WaIcon = __decorateClass([
  t("wa-icon")
], WaIcon);

export {
  WaErrorEvent,
  WaIcon
};
