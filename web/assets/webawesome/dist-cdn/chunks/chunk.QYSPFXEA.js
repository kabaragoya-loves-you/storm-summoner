/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  scrollIntoView
} from "./chunk.GB2I2DTW.js";
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
  t
} from "./chunk.7F6EHFYD.js";
import {
  x
} from "./chunk.23WMFJHA.js";
import {
  __decorateClass
} from "./chunk.6E4D3PD7.js";

// src/events/tab-hide.ts
var WaTabHideEvent = class extends Event {
  constructor(detail) {
    super("wa-tab-hide", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};

// src/events/tab-show.ts
var WaTabShowEvent = class extends Event {
  constructor(detail) {
    super("wa-tab-show", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};

// src/components/tab-group/tab-group.css
var tab_group_default = ":host {\n  --indicator-color: var(--wa-color-brand-fill-loud);\n  --track-color: var(--wa-color-neutral-fill-normal);\n  --track-width: 0.125rem;\n\n  display: block;\n}\n\n.tab-group {\n  display: flex;\n  border-radius: 0;\n}\n\n.tabs {\n  display: flex;\n  position: relative;\n}\n\n.indicator {\n  position: absolute;\n}\n\n.tab-group-has-scroll-controls .nav-container {\n  position: relative;\n  padding: 0 1.5em;\n}\n\n.body {\n  display: block;\n}\n\n.scroll-button {\n  display: flex;\n  align-items: center;\n  justify-content: center;\n  position: absolute;\n  top: 0;\n  bottom: 0;\n  width: 1.5em;\n}\n\n.scroll-button-start {\n  inset-inline-start: 0;\n}\n\n.scroll-button-end {\n  inset-inline-end: 0;\n}\n\n/*\n   * Top\n   */\n\n.tab-group-top {\n  flex-direction: column;\n}\n\n.tab-group-top .nav-container {\n  order: 1;\n}\n\n.tab-group-top .nav {\n  display: flex;\n  overflow-x: auto;\n\n  /* Hide scrollbar in Firefox */\n  scrollbar-width: none;\n}\n\n/* Hide scrollbar in Chrome/Safari */\n.tab-group-top .nav::-webkit-scrollbar {\n  width: 0;\n  height: 0;\n}\n\n.tab-group-top .tabs {\n  flex: 1 1 auto;\n  position: relative;\n  flex-direction: row;\n  border-bottom: solid var(--track-width) var(--track-color);\n}\n\n.tab-group-top .indicator {\n  bottom: calc(-1 * var(--track-width));\n  border-bottom: solid var(--track-width) var(--indicator-color);\n}\n\n.tab-group-top .body {\n  order: 2;\n}\n\n.tab-group-top ::slotted(wa-tab[active]) {\n  border-block-end: solid var(--track-width) var(--indicator-color);\n  margin-block-end: calc(-1 * var(--track-width));\n}\n\n.tab-group-top ::slotted(wa-tab-panel) {\n  --padding: var(--wa-space-xl) 0;\n}\n\n/*\n   * Bottom\n   */\n\n.tab-group-bottom {\n  flex-direction: column;\n}\n\n.tab-group-bottom .nav-container {\n  order: 2;\n}\n\n.tab-group-bottom .nav {\n  display: flex;\n  overflow-x: auto;\n\n  /* Hide scrollbar in Firefox */\n  scrollbar-width: none;\n}\n\n/* Hide scrollbar in Chrome/Safari */\n.tab-group-bottom .nav::-webkit-scrollbar {\n  width: 0;\n  height: 0;\n}\n\n.tab-group-bottom .tabs {\n  flex: 1 1 auto;\n  position: relative;\n  flex-direction: row;\n  border-top: solid var(--track-width) var(--track-color);\n}\n\n.tab-group-bottom .indicator {\n  top: calc(-1 * var(--track-width));\n  border-top: solid var(--track-width) var(--indicator-color);\n}\n\n.tab-group-bottom .body {\n  order: 1;\n}\n\n.tab-group-bottom ::slotted(wa-tab[active]) {\n  border-block-start: solid var(--track-width) var(--indicator-color);\n  margin-block-start: calc(-1 * var(--track-width));\n}\n\n.tab-group-bottom ::slotted(wa-tab-panel) {\n  --padding: var(--wa-space-xl) 0;\n}\n\n/*\n   * Start\n   */\n\n.tab-group-start {\n  flex-direction: row;\n}\n\n.tab-group-start .nav-container {\n  order: 1;\n}\n\n.tab-group-start .tabs {\n  flex: 0 0 auto;\n  flex-direction: column;\n  border-inline-end: solid var(--track-width) var(--track-color);\n}\n\n.tab-group-start .indicator {\n  inset-inline-end: calc(-1 * var(--track-width));\n  border-right: solid var(--track-width) var(--indicator-color);\n}\n\n.tab-group-start .body {\n  flex: 1 1 auto;\n  order: 2;\n}\n\n.tab-group-start ::slotted(wa-tab[active]) {\n  border-inline-end: solid var(--track-width) var(--indicator-color);\n  margin-inline-end: calc(-1 * var(--track-width));\n}\n\n.tab-group-start ::slotted(wa-tab-panel) {\n  --padding: 0 var(--wa-space-xl);\n}\n\n/*\n   * End\n   */\n\n.tab-group-end {\n  flex-direction: row;\n}\n\n.tab-group-end .nav-container {\n  order: 2;\n}\n\n.tab-group-end .tabs {\n  flex: 0 0 auto;\n  flex-direction: column;\n  border-left: solid var(--track-width) var(--track-color);\n}\n\n.tab-group-end .indicator {\n  inset-inline-start: calc(-1 * var(--track-width));\n  border-inline-start: solid var(--track-width) var(--indicator-color);\n}\n\n.tab-group-end .body {\n  flex: 1 1 auto;\n  order: 1;\n}\n\n.tab-group-end ::slotted(wa-tab[active]) {\n  border-inline-start: solid var(--track-width) var(--indicator-color);\n  margin-inline-start: calc(-1 * var(--track-width));\n}\n\n.tab-group-end ::slotted(wa-tab-panel) {\n  --padding: 0 var(--wa-space-xl);\n}\n";

// src/components/tab-group/tab-group.ts
var WaTabGroup = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.tabs = [];
    this.focusableTabs = [];
    this.panels = [];
    this.localize = new LocalizeController(this);
    this.hasScrollControls = false;
    this.active = "";
    this.placement = "top";
    this.activation = "auto";
    this.withoutScrollControls = false;
  }
  connectedCallback() {
    super.connectedCallback();
    this.resizeObserver = new ResizeObserver(() => {
      this.updateScrollControls();
    });
    this.mutationObserver = new MutationObserver((mutations) => {
      if (mutations.some((m) => !["aria-labelledby", "aria-controls"].includes(m.attributeName))) {
        setTimeout(() => this.setAriaLabels());
      }
      const relevantMutations = mutations.filter((m) => {
        const target = m.target;
        return target.closest("wa-tab-group") === this;
      });
      if (relevantMutations.some((m) => m.attributeName === "disabled")) {
        this.syncTabsAndPanels();
      } else if (relevantMutations.some((m) => m.attributeName === "active")) {
        const tabs = relevantMutations.filter((m) => m.attributeName === "active" && m.target.tagName.toLowerCase() === "wa-tab").map((m) => m.target);
        const newActiveTab = tabs.find((tab) => tab.active);
        if (newActiveTab && newActiveTab.closest("wa-tab-group") === this) {
          this.setActiveTab(newActiveTab);
        }
      }
    });
    this.updateComplete.then(() => {
      this.syncTabsAndPanels();
      this.mutationObserver.observe(this, { attributes: true, childList: true, subtree: true });
      this.resizeObserver.observe(this.nav);
      const intersectionObserver = new IntersectionObserver((entries, observer) => {
        if (entries[0].intersectionRatio > 0) {
          this.setAriaLabels();
          if (this.active) {
            const tab = this.tabs.find((t2) => t2.panel === this.active);
            if (tab) {
              this.setActiveTab(tab);
            }
          } else {
            this.setActiveTab(this.getActiveTab() ?? this.tabs[0], { emitEvents: false });
          }
          observer.unobserve(entries[0].target);
        }
      });
      intersectionObserver.observe(this.tabGroup);
    });
  }
  disconnectedCallback() {
    super.disconnectedCallback();
    this.mutationObserver?.disconnect();
    if (this.nav) {
      this.resizeObserver?.unobserve(this.nav);
    }
  }
  getAllTabs() {
    const slot = this.shadowRoot.querySelector('slot[name="nav"]');
    return [...slot.assignedElements()].filter((el) => {
      return el.tagName.toLowerCase() === "wa-tab";
    });
  }
  getAllPanels() {
    return [...this.body.assignedElements()].filter((el) => el.tagName.toLowerCase() === "wa-tab-panel");
  }
  getActiveTab() {
    return this.tabs.find((el) => el.active);
  }
  handleClick(event) {
    const target = event.target;
    const tab = target.closest("wa-tab");
    const tabGroup = tab?.closest("wa-tab-group");
    if (tabGroup !== this) {
      return;
    }
    if (tab !== null) {
      this.setActiveTab(tab, { scrollBehavior: "smooth" });
    }
  }
  handleKeyDown(event) {
    const target = event.target;
    const tab = target.closest("wa-tab");
    const tabGroup = tab?.closest("wa-tab-group");
    if (tabGroup !== this) {
      return;
    }
    if (["Enter", " "].includes(event.key)) {
      if (tab !== null) {
        this.setActiveTab(tab, { scrollBehavior: "smooth" });
        event.preventDefault();
      }
      return;
    }
    if (["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "Home", "End"].includes(event.key)) {
      const activeEl = this.tabs.find((t2) => t2.matches(":focus"));
      const isRtl = this.localize.dir() === "rtl";
      let nextTab = null;
      if (activeEl?.tagName.toLowerCase() === "wa-tab") {
        if (event.key === "Home") {
          nextTab = this.focusableTabs[0];
        } else if (event.key === "End") {
          nextTab = this.focusableTabs[this.focusableTabs.length - 1];
        } else if (["top", "bottom"].includes(this.placement) && event.key === (isRtl ? "ArrowRight" : "ArrowLeft") || ["start", "end"].includes(this.placement) && event.key === "ArrowUp") {
          const currentIndex = this.tabs.findIndex((el) => el === activeEl);
          nextTab = this.findNextFocusableTab(currentIndex, "backward");
        } else if (["top", "bottom"].includes(this.placement) && event.key === (isRtl ? "ArrowLeft" : "ArrowRight") || ["start", "end"].includes(this.placement) && event.key === "ArrowDown") {
          const currentIndex = this.tabs.findIndex((el) => el === activeEl);
          nextTab = this.findNextFocusableTab(currentIndex, "forward");
        }
        if (!nextTab) {
          return;
        }
        nextTab.tabIndex = 0;
        nextTab.focus({ preventScroll: true });
        if (this.activation === "auto") {
          this.setActiveTab(nextTab, { scrollBehavior: "smooth" });
        } else {
          this.tabs.forEach((tabEl) => {
            tabEl.tabIndex = tabEl === nextTab ? 0 : -1;
          });
        }
        if (["top", "bottom"].includes(this.placement)) {
          scrollIntoView(nextTab, this.nav, "horizontal");
        }
        event.preventDefault();
      }
    }
  }
  findNextFocusableTab(currentIndex, direction) {
    let nextTab = null;
    const iterator = direction === "forward" ? 1 : -1;
    let nextIndex = currentIndex + iterator;
    while (currentIndex < this.tabs.length) {
      nextTab = this.tabs[nextIndex] || null;
      if (nextTab === null) {
        if (direction === "forward") {
          nextTab = this.focusableTabs[0];
        } else {
          nextTab = this.focusableTabs[this.focusableTabs.length - 1];
        }
        break;
      }
      if (!nextTab.disabled) {
        break;
      }
      nextIndex += iterator;
    }
    return nextTab;
  }
  handleScrollToStart() {
    this.nav.scroll({
      left: this.localize.dir() === "rtl" ? this.nav.scrollLeft + this.nav.clientWidth : this.nav.scrollLeft - this.nav.clientWidth,
      behavior: "smooth"
    });
  }
  handleScrollToEnd() {
    this.nav.scroll({
      left: this.localize.dir() === "rtl" ? this.nav.scrollLeft - this.nav.clientWidth : this.nav.scrollLeft + this.nav.clientWidth,
      behavior: "smooth"
    });
  }
  setActiveTab(tab, options) {
    options = {
      emitEvents: true,
      scrollBehavior: "auto",
      ...options
    };
    if (tab.closest("wa-tab-group") !== this) {
      return;
    }
    if (tab !== this.activeTab && !tab.disabled) {
      const previousTab = this.activeTab;
      this.active = tab.panel;
      this.activeTab = tab;
      this.tabs.forEach((el) => {
        el.active = el === this.activeTab;
        el.tabIndex = el === this.activeTab ? 0 : -1;
      });
      this.panels.forEach((el) => el.active = el.name === this.activeTab?.panel);
      if (["top", "bottom"].includes(this.placement)) {
        scrollIntoView(this.activeTab, this.nav, "horizontal", options.scrollBehavior);
      }
      if (options.emitEvents) {
        if (previousTab) {
          this.dispatchEvent(new WaTabHideEvent({ name: previousTab.panel }));
        }
        this.dispatchEvent(new WaTabShowEvent({ name: this.activeTab.panel }));
      }
    }
  }
  setAriaLabels() {
    this.tabs.forEach((tab) => {
      const panel = this.panels.find((el) => el.name === tab.panel);
      if (panel) {
        tab.setAttribute("aria-controls", panel.getAttribute("id"));
        panel.setAttribute("aria-labelledby", tab.getAttribute("id"));
      }
    });
  }
  // This stores tabs and panels so we can refer to a cache instead of calling querySelectorAll() multiple times.
  syncTabsAndPanels() {
    this.tabs = this.getAllTabs();
    this.focusableTabs = this.tabs.filter((el) => !el.disabled);
    this.panels = this.getAllPanels();
    this.updateComplete.then(() => this.updateScrollControls());
  }
  updateActiveTab() {
    const tab = this.tabs.find((el) => el.panel === this.active);
    if (tab) {
      this.setActiveTab(tab, { scrollBehavior: "smooth" });
    }
  }
  updateScrollControls() {
    if (this.withoutScrollControls) {
      this.hasScrollControls = false;
    } else {
      this.hasScrollControls = ["top", "bottom"].includes(this.placement) && this.nav.scrollWidth > this.nav.clientWidth + 1;
    }
  }
  render() {
    const isRtl = this.hasUpdated ? this.localize.dir() === "rtl" : this.dir === "rtl";
    return x`
      <div
        part="base"
        class=${e2({
      "tab-group": true,
      "tab-group-top": this.placement === "top",
      "tab-group-bottom": this.placement === "bottom",
      "tab-group-start": this.placement === "start",
      "tab-group-end": this.placement === "end",
      "tab-group-has-scroll-controls": this.hasScrollControls
    })}
        @click=${this.handleClick}
        @keydown=${this.handleKeyDown}
      >
        <div class="nav-container" part="nav">
          ${this.hasScrollControls ? x`
                <wa-button
                  part="scroll-button scroll-button-start"
                  exportparts="base:scroll-button__base"
                  class="scroll-button scroll-button-start"
                  appearance="plain"
                  @click=${this.handleScrollToStart}
                >
                  <wa-icon
                    name=${isRtl ? "chevron-right" : "chevron-left"}
                    library="system"
                    variant="solid"
                    label=${this.localize.term("scrollToStart")}
                  ></wa-icon>
                </wa-button>
              ` : ""}

          <!-- We have a focus listener because in Firefox (and soon to be Chrome) overflow containers are focusable. -->
          <div class="nav" @focus=${() => this.activeTab?.focus({ preventScroll: true })}>
            <div part="tabs" class="tabs" role="tablist">
              <slot name="nav" @slotchange=${this.syncTabsAndPanels}></slot>
            </div>
          </div>

          ${this.hasScrollControls ? x`
                <wa-button
                  part="scroll-button scroll-button-end"
                  class="scroll-button scroll-button-end"
                  exportparts="base:scroll-button__base"
                  appearance="plain"
                  @click=${this.handleScrollToEnd}
                >
                  <wa-icon
                    name=${isRtl ? "chevron-left" : "chevron-right"}
                    library="system"
                    variant="solid"
                    label=${this.localize.term("scrollToEnd")}
                  ></wa-icon>
                </wa-button>
              ` : ""}
        </div>

        <slot part="body" class="body" @slotchange=${this.syncTabsAndPanels}></slot>
      </div>
    `;
  }
};
WaTabGroup.css = tab_group_default;
__decorateClass([
  e(".tab-group")
], WaTabGroup.prototype, "tabGroup", 2);
__decorateClass([
  e(".body")
], WaTabGroup.prototype, "body", 2);
__decorateClass([
  e(".nav")
], WaTabGroup.prototype, "nav", 2);
__decorateClass([
  r()
], WaTabGroup.prototype, "hasScrollControls", 2);
__decorateClass([
  n({ reflect: true })
], WaTabGroup.prototype, "active", 2);
__decorateClass([
  n()
], WaTabGroup.prototype, "placement", 2);
__decorateClass([
  n()
], WaTabGroup.prototype, "activation", 2);
__decorateClass([
  n({ attribute: "without-scroll-controls", type: Boolean })
], WaTabGroup.prototype, "withoutScrollControls", 2);
__decorateClass([
  watch("active")
], WaTabGroup.prototype, "updateActiveTab", 1);
__decorateClass([
  watch("withoutScrollControls", { waitUntilFirstUpdate: true })
], WaTabGroup.prototype, "updateScrollControls", 1);
WaTabGroup = __decorateClass([
  t("wa-tab-group")
], WaTabGroup);

export {
  WaTabGroup
};
