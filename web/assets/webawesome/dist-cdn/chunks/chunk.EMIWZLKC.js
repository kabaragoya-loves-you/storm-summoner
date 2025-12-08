/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  requestInclude
} from "./chunk.PGQDA7UX.js";
import {
  WaLoadEvent
} from "./chunk.YNHANM3W.js";
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

// src/events/include-error.ts
var WaIncludeErrorEvent = class extends Event {
  constructor(detail) {
    super("wa-include-error", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};

// src/components/include/include.css
var include_default = ":host {\n  display: block;\n}\n";

// src/components/include/include.ts
var WaInclude = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.mode = "cors";
    this.allowScripts = false;
  }
  executeScript(script) {
    const newScript = document.createElement("script");
    [...script.attributes].forEach((attr) => newScript.setAttribute(attr.name, attr.value));
    newScript.textContent = script.textContent;
    script.parentNode.replaceChild(newScript, script);
  }
  async handleSrcChange() {
    try {
      const src = this.src;
      const file = await requestInclude(src, this.mode);
      if (src !== this.src) {
        return;
      }
      if (!file.ok) {
        this.dispatchEvent(new WaIncludeErrorEvent({ status: file.status }));
        return;
      }
      this.innerHTML = file.html;
      if (this.allowScripts) {
        [...this.querySelectorAll("script")].forEach((script) => this.executeScript(script));
      }
      this.dispatchEvent(new WaLoadEvent());
    } catch {
      this.dispatchEvent(new WaIncludeErrorEvent({ status: -1 }));
    }
  }
  render() {
    return x`<slot></slot>`;
  }
};
WaInclude.css = include_default;
__decorateClass([
  n()
], WaInclude.prototype, "src", 2);
__decorateClass([
  n()
], WaInclude.prototype, "mode", 2);
__decorateClass([
  n({ attribute: "allow-scripts", type: Boolean })
], WaInclude.prototype, "allowScripts", 2);
__decorateClass([
  watch("src")
], WaInclude.prototype, "handleSrcChange", 1);
WaInclude = __decorateClass([
  t("wa-include")
], WaInclude);

export {
  WaInclude
};
