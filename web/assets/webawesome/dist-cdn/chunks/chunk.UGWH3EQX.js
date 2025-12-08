/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  clamp
} from "./chunk.KABXVFSA.js";
import {
  waitForEvent
} from "./chunk.MDCY6RAP.js";
import {
  prefersReducedMotion
} from "./chunk.EFWMJOG7.js";
import {
  e as e2
} from "./chunk.6MY2PWCO.js";
import {
  AutoplayController
} from "./chunk.RJ6AVKBN.js";
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
  o,
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

// src/internal/scrollend-polyfill.ts
(() => {
  if (o) {
    return;
  }
  const debounce = (fn, delay) => {
    let timerId = 0;
    return function(...args) {
      window.clearTimeout(timerId);
      timerId = window.setTimeout(() => {
        fn.call(this, ...args);
      }, delay);
    };
  };
  const decorate = (proto, method, decorateFn) => {
    const superFn = proto[method];
    proto[method] = function(...args) {
      superFn.call(this, ...args);
      decorateFn.call(this, superFn, ...args);
    };
  };
  const isSupported = "onscrollend" in window;
  if (!isSupported) {
    const pointers = /* @__PURE__ */ new Set();
    const scrollHandlers = /* @__PURE__ */ new WeakMap();
    const handlePointerDown = (event) => {
      pointers.add(event.pointerId);
    };
    const handlePointerUp = (event) => {
      pointers.delete(event.pointerId);
    };
    document.addEventListener("pointerdown", handlePointerDown);
    document.addEventListener("pointerup", handlePointerUp);
    decorate(EventTarget.prototype, "addEventListener", function(addEventListener, type) {
      if (type !== "scroll") return;
      const handleScrollEnd = debounce(() => {
        if (!pointers.size) {
          this.dispatchEvent(new Event("scrollend"));
        } else {
          handleScrollEnd();
        }
      }, 100);
      addEventListener.call(this, "scroll", handleScrollEnd, { passive: true });
      scrollHandlers.set(this, handleScrollEnd);
    });
    decorate(EventTarget.prototype, "removeEventListener", function(removeEventListener, type) {
      if (type !== "scroll") return;
      const scrollHandler = scrollHandlers.get(this);
      if (scrollHandler) {
        removeEventListener.call(this, "scroll", scrollHandler, { passive: true });
      }
    });
  }
})();

// ../../node_modules/lit-html/directives/map.js
function* o2(o4, f) {
  if (void 0 !== o4) {
    let i = 0;
    for (const t3 of o4) yield f(t3, i++);
  }
}

// ../../node_modules/lit-html/directives/range.js
function* o3(o4, t3, e3 = 1) {
  const i = void 0 === t3 ? 0 : o4;
  t3 ?? (t3 = o4);
  for (let o5 = i; e3 > 0 ? o5 < t3 : t3 < o5; o5 += e3) yield o5;
}

// src/events/slide-change.ts
var WaSlideChangeEvent = class extends Event {
  constructor(detail) {
    super("wa-slide-change", { bubbles: true, cancelable: false, composed: true });
    this.detail = detail;
  }
};

// src/components/carousel/carousel.css
var carousel_default = ":host {\n  --aspect-ratio: 16 / 9;\n  --scroll-hint: 0px;\n  --slide-gap: var(--wa-space-m, 1rem); /* fallback value is necessary */\n\n  display: flex;\n}\n\n.carousel {\n  display: grid;\n  grid-template-columns: min-content 1fr min-content;\n  grid-template-rows: 1fr min-content;\n  grid-template-areas:\n    '. slides .'\n    '. pagination .';\n  gap: var(--wa-space-m);\n  align-items: center;\n  min-height: 100%;\n  min-width: 100%;\n  position: relative;\n}\n\n.pagination {\n  grid-area: pagination;\n  display: flex;\n  flex-wrap: wrap;\n  justify-content: center;\n  gap: var(--wa-space-s);\n}\n\n.slides {\n  grid-area: slides;\n\n  display: grid;\n  height: 100%;\n  width: 100%;\n  align-items: center;\n  justify-items: center;\n  overflow: auto;\n  overscroll-behavior-x: contain;\n  scrollbar-width: none;\n  aspect-ratio: calc(var(--aspect-ratio) * var(--slides-per-page));\n  border-radius: var(--wa-border-radius-m);\n\n  --slide-size: calc((100% - (var(--slides-per-page) - 1) * var(--slide-gap)) / var(--slides-per-page));\n}\n\n@media (prefers-reduced-motion) {\n  :where(.slides) {\n    scroll-behavior: auto;\n  }\n}\n\n.slides-horizontal {\n  grid-auto-flow: column;\n  grid-auto-columns: var(--slide-size);\n  grid-auto-rows: 100%;\n  column-gap: var(--slide-gap);\n  scroll-snap-type: x mandatory;\n  scroll-padding-inline: var(--scroll-hint);\n  padding-inline: var(--scroll-hint);\n  overflow-y: hidden;\n}\n\n.slides-vertical {\n  grid-auto-flow: row;\n  grid-auto-columns: 100%;\n  grid-auto-rows: var(--slide-size);\n  row-gap: var(--slide-gap);\n  scroll-snap-type: y mandatory;\n  scroll-padding-block: var(--scroll-hint);\n  padding-block: var(--scroll-hint);\n  overflow-x: hidden;\n}\n\n.slides-dragging,\n.slides-dropping {\n  scroll-snap-type: unset;\n}\n\n:host([vertical]) ::slotted(wa-carousel-item) {\n  height: 100%;\n}\n\n.slides::-webkit-scrollbar {\n  display: none;\n}\n\n.navigation {\n  grid-area: navigation;\n  display: contents;\n  font-size: var(--wa-font-size-l);\n}\n\n.navigation-button {\n  flex: 0 0 auto;\n  display: flex;\n  align-items: center;\n  background: none;\n  border: none;\n  border-radius: var(--wa-border-radius-m);\n  font-size: inherit;\n  color: var(--wa-color-text-quiet);\n  padding: var(--wa-space-xs);\n  cursor: pointer;\n  transition: var(--wa-transition-normal) color;\n  appearance: none;\n}\n\n.navigation-button-disabled {\n  opacity: 0.5;\n  cursor: not-allowed;\n}\n\n.navigation-button-disabled::part(base) {\n  pointer-events: none;\n}\n\n.navigation-button-previous {\n  grid-column: 1;\n  grid-row: 1;\n}\n\n.navigation-button-next {\n  grid-column: 3;\n  grid-row: 1;\n}\n\n.pagination-item {\n  display: block;\n  cursor: pointer;\n  background: none;\n  border: 0;\n  border-radius: var(--wa-border-radius-circle);\n  width: var(--wa-space-s);\n  height: var(--wa-space-s);\n  background-color: var(--wa-color-neutral-fill-normal);\n  padding: 0;\n  margin: 0;\n  transition: transform var(--wa-transition-slow);\n}\n\n.pagination-item-active {\n  background-color: var(--wa-form-control-activated-color);\n  transform: scale(1.25);\n}\n\n/* Focus styles */\n.slides:focus-visible,\n.navigation-button:focus-visible,\n.pagination-item:focus-visible {\n  outline: var(--wa-focus-ring);\n  outline-offset: var(--wa-focus-ring-offset);\n}\n";

// src/components/carousel/carousel.ts
var WaCarousel = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.loop = false;
    this.slides = 0;
    this.currentSlide = 0;
    this.navigation = false;
    this.pagination = false;
    this.autoplay = false;
    this.autoplayInterval = 3e3;
    this.slidesPerPage = 1;
    this.slidesPerMove = 1;
    this.orientation = "horizontal";
    this.mouseDragging = false;
    this.activeSlide = 0;
    this.scrolling = false;
    this.dragging = false;
    this.autoplayController = new AutoplayController(this, () => this.next());
    this.dragStartPosition = [-1, -1];
    this.localize = new LocalizeController(this);
    this.pendingSlideChange = false;
    this.handleMouseDrag = (event) => {
      if (!this.dragging) {
        this.scrollContainer.style.setProperty("scroll-snap-type", "none");
        this.dragging = true;
        this.dragStartPosition = [event.clientX, event.clientY];
      }
      this.scrollContainer.scrollBy({
        left: -event.movementX,
        top: -event.movementY,
        behavior: "instant"
      });
    };
    this.handleMouseDragEnd = () => {
      const scrollContainer = this.scrollContainer;
      document.removeEventListener("pointermove", this.handleMouseDrag, { capture: true });
      const startLeft = scrollContainer.scrollLeft;
      const startTop = scrollContainer.scrollTop;
      scrollContainer.style.removeProperty("scroll-snap-type");
      scrollContainer.style.setProperty("overflow", "hidden");
      const finalLeft = scrollContainer.scrollLeft;
      const finalTop = scrollContainer.scrollTop;
      scrollContainer.style.removeProperty("overflow");
      scrollContainer.style.setProperty("scroll-snap-type", "none");
      scrollContainer.scrollTo({ left: startLeft, top: startTop, behavior: "instant" });
      requestAnimationFrame(async () => {
        if (startLeft !== finalLeft || startTop !== finalTop) {
          scrollContainer.scrollTo({
            left: finalLeft,
            top: finalTop,
            behavior: prefersReducedMotion() ? "auto" : "smooth"
          });
          await waitForEvent(scrollContainer, "scrollend");
        }
        scrollContainer.style.removeProperty("scroll-snap-type");
        this.dragging = false;
        this.dragStartPosition = [-1, -1];
        this.handleScrollEnd();
      });
    };
    this.handleSlotChange = (mutations) => {
      const needsInitialization = mutations.some(
        (mutation) => [...mutation.addedNodes, ...mutation.removedNodes].some(
          (el) => this.isCarouselItem(el) && !el.hasAttribute("data-clone")
        )
      );
      if (needsInitialization) {
        this.initializeSlides();
      }
      this.requestUpdate();
    };
  }
  connectedCallback() {
    super.connectedCallback();
    this.setAttribute("role", "region");
    this.setAttribute("aria-label", this.localize.term("carousel"));
  }
  disconnectedCallback() {
    super.disconnectedCallback();
    this.mutationObserver?.disconnect();
  }
  firstUpdated() {
    this.initializeSlides();
    this.mutationObserver = new MutationObserver(this.handleSlotChange);
    this.mutationObserver.observe(this, {
      childList: true,
      subtree: true
    });
  }
  willUpdate(changedProperties) {
    if (changedProperties.has("slidesPerMove") || changedProperties.has("slidesPerPage")) {
      this.slidesPerMove = Math.min(this.slidesPerMove, this.slidesPerPage);
    }
  }
  getPageCount() {
    const slidesCount = this.getSlides().length;
    const { slidesPerPage, slidesPerMove, loop } = this;
    const pages = loop ? slidesCount / slidesPerMove : (slidesCount - slidesPerPage) / slidesPerMove + 1;
    return Math.ceil(pages);
  }
  getCurrentPage() {
    return Math.ceil(this.activeSlide / this.slidesPerMove);
  }
  canScrollNext() {
    return this.loop || this.getCurrentPage() < this.getPageCount() - 1;
  }
  canScrollPrev() {
    return this.loop || this.getCurrentPage() > 0;
  }
  /** @internal Gets all carousel items. */
  getSlides({ excludeClones = true } = {}) {
    return [...this.children].filter(
      (el) => this.isCarouselItem(el) && (!excludeClones || !el.hasAttribute("data-clone"))
    );
  }
  handleClick(event) {
    if (this.dragging && this.dragStartPosition[0] > 0 && this.dragStartPosition[1] > 0) {
      const deltaX = Math.abs(this.dragStartPosition[0] - event.clientX);
      const deltaY = Math.abs(this.dragStartPosition[1] - event.clientY);
      const delta = Math.sqrt(deltaX * deltaX + deltaY * deltaY);
      if (delta >= 10) {
        event.preventDefault();
      }
    }
  }
  handleKeyDown(event) {
    if (["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "Home", "End"].includes(event.key)) {
      const target = event.target;
      const isRtl = this.localize.dir() === "rtl";
      const isFocusInPagination = target.closest('[part~="pagination-item"]') !== null;
      const isNext = event.key === "ArrowDown" || !isRtl && event.key === "ArrowRight" || isRtl && event.key === "ArrowLeft";
      const isPrevious = event.key === "ArrowUp" || !isRtl && event.key === "ArrowLeft" || isRtl && event.key === "ArrowRight";
      event.preventDefault();
      if (isPrevious) {
        this.previous();
      }
      if (isNext) {
        this.next();
      }
      if (event.key === "Home") {
        this.goToSlide(0);
      }
      if (event.key === "End") {
        this.goToSlide(this.getSlides().length - 1);
      }
      if (isFocusInPagination) {
        this.updateComplete.then(() => {
          const activePaginationItem = this.shadowRoot?.querySelector(
            '[part~="pagination-item-active"]'
          );
          if (activePaginationItem) {
            activePaginationItem.focus();
          }
        });
      }
    }
  }
  handleMouseDragStart(event) {
    const canDrag = this.mouseDragging && event.button === 0;
    if (canDrag) {
      event.preventDefault();
      document.addEventListener("pointermove", this.handleMouseDrag, { capture: true, passive: true });
      document.addEventListener("pointerup", this.handleMouseDragEnd, { capture: true, once: true });
    }
  }
  handleScroll() {
    this.scrolling = true;
    if (!this.pendingSlideChange) {
      this.synchronizeSlides();
    }
  }
  /** @internal Synchronizes the slides with the IntersectionObserver API. */
  synchronizeSlides() {
    const io = new IntersectionObserver(
      (entries) => {
        io.disconnect();
        for (const entry of entries) {
          const slide = entry.target;
          slide.toggleAttribute("inert", !entry.isIntersecting);
          slide.classList.toggle("--in-view", entry.isIntersecting);
          slide.setAttribute("aria-hidden", entry.isIntersecting ? "false" : "true");
        }
        const firstIntersecting = entries.find((entry) => entry.isIntersecting);
        if (!firstIntersecting) {
          return;
        }
        const slidesWithClones = this.getSlides({ excludeClones: false });
        const slidesCount = this.getSlides().length;
        const slideIndex = slidesWithClones.indexOf(firstIntersecting.target);
        const normalizedIndex = this.loop ? slideIndex - this.slidesPerPage : slideIndex;
        if (firstIntersecting) {
          this.activeSlide = (Math.ceil(normalizedIndex / this.slidesPerMove) * this.slidesPerMove + slidesCount) % slidesCount;
          if (!this.scrolling) {
            if (this.loop && firstIntersecting.target.hasAttribute("data-clone")) {
              const clonePosition = Number(firstIntersecting.target.getAttribute("data-clone"));
              this.goToSlide(clonePosition, "instant");
            }
          }
        }
      },
      {
        root: this.scrollContainer,
        threshold: 0.6
      }
    );
    this.getSlides({ excludeClones: false }).forEach((slide) => {
      io.observe(slide);
    });
  }
  handleScrollEnd() {
    if (!this.scrolling || this.dragging) return;
    this.synchronizeSlides();
    this.scrolling = false;
    this.pendingSlideChange = false;
    this.synchronizeSlides();
  }
  isCarouselItem(node) {
    return node instanceof Element && node.tagName.toLowerCase() === "wa-carousel-item";
  }
  initializeSlides() {
    this.getSlides({ excludeClones: false }).forEach((slide, index) => {
      slide.classList.remove("--in-view");
      slide.classList.remove("--is-active");
      slide.setAttribute("aria-label", this.localize.term("slideNum", index + 1));
      if (slide.hasAttribute("data-clone")) {
        slide.remove();
      }
    });
    this.updateSlidesSnap();
    if (this.loop) {
      this.createClones();
    }
    this.goToSlide(this.activeSlide, "auto");
    this.synchronizeSlides();
  }
  createClones() {
    const slides = this.getSlides();
    const slidesPerPage = this.slidesPerPage;
    const lastSlides = slides.slice(-slidesPerPage);
    const firstSlides = slides.slice(0, slidesPerPage);
    lastSlides.reverse().forEach((slide, i) => {
      const clone = slide.cloneNode(true);
      clone.setAttribute("data-clone", String(slides.length - i - 1));
      this.prepend(clone);
    });
    firstSlides.forEach((slide, i) => {
      const clone = slide.cloneNode(true);
      clone.setAttribute("data-clone", String(i));
      this.append(clone);
    });
  }
  handleSlideChange() {
    const slides = this.getSlides();
    slides.forEach((slide, i) => {
      slide.classList.toggle("--is-active", i === this.activeSlide);
    });
    if (this.hasUpdated) {
      this.dispatchEvent(
        new WaSlideChangeEvent({
          index: this.activeSlide,
          slide: slides[this.activeSlide]
        })
      );
    }
  }
  updateSlidesSnap() {
    const slides = this.getSlides();
    const slidesPerMove = this.slidesPerMove;
    slides.forEach((slide, i) => {
      const shouldSnap = (i + slidesPerMove) % slidesPerMove === 0;
      if (shouldSnap) {
        slide.style.removeProperty("scroll-snap-align");
      } else {
        slide.style.setProperty("scroll-snap-align", "none");
      }
    });
  }
  handleAutoplayChange() {
    this.autoplayController.stop();
    if (this.autoplay) {
      this.autoplayController.start(this.autoplayInterval);
    }
  }
  /**
   * Move the carousel backward by `slides-per-move` slides.
   *
   * @param behavior - The behavior used for scrolling.
   */
  previous(behavior = "smooth") {
    this.goToSlide(this.activeSlide - this.slidesPerMove, behavior);
  }
  /**
   * Move the carousel forward by `slides-per-move` slides.
   *
   * @param behavior - The behavior used for scrolling.
   */
  next(behavior = "smooth") {
    this.goToSlide(this.activeSlide + this.slidesPerMove, behavior);
  }
  /**
   * Scrolls the carousel to the slide specified by `index`.
   *
   * @param index - The slide index.
   * @param behavior - The behavior used for scrolling.
   */
  goToSlide(index, behavior = "smooth") {
    const { slidesPerPage, loop } = this;
    const slides = this.getSlides();
    const slidesWithClones = this.getSlides({ excludeClones: false });
    if (!slides.length) {
      return;
    }
    const newActiveSlide = loop ? (index + slides.length) % slides.length : clamp(index, 0, slides.length - slidesPerPage);
    this.activeSlide = newActiveSlide;
    const isRtl = this.localize.dir() === "rtl";
    const nextSlideIndex = clamp(
      index + (loop ? slidesPerPage : 0) + (isRtl ? slidesPerPage - 1 : 0),
      0,
      slidesWithClones.length - 1
    );
    const nextSlide = slidesWithClones[nextSlideIndex];
    this.scrollToSlide(nextSlide, prefersReducedMotion() ? "auto" : behavior);
  }
  scrollToSlide(slide, behavior = "smooth") {
    this.pendingSlideChange = true;
    window.requestAnimationFrame(() => {
      if (!this.scrollContainer) {
        return;
      }
      const scrollContainer = this.scrollContainer;
      const scrollContainerRect = scrollContainer.getBoundingClientRect();
      const nextSlideRect = slide.getBoundingClientRect();
      const nextLeft = nextSlideRect.left - scrollContainerRect.left;
      const nextTop = nextSlideRect.top - scrollContainerRect.top;
      if (nextLeft || nextTop) {
        this.pendingSlideChange = true;
        scrollContainer.scrollTo({
          left: nextLeft + scrollContainer.scrollLeft,
          top: nextTop + scrollContainer.scrollTop,
          behavior
        });
      } else {
        this.pendingSlideChange = false;
      }
    });
  }
  render() {
    const { slidesPerMove, scrolling } = this;
    let pagesCount = 0;
    let currentPage = 0;
    let prevEnabled = false;
    let nextEnabled = false;
    if (this.hasUpdated) {
      pagesCount = this.getPageCount();
      currentPage = this.getCurrentPage();
      prevEnabled = this.canScrollPrev();
      nextEnabled = this.canScrollNext();
    }
    const isRTL = o ? this.dir === "rtl" : this.localize.dir() === "rtl";
    return x`
      <div part="base" class="carousel">
        <div
          id="scroll-container"
          part="scroll-container"
          class="${e2({
      slides: true,
      "slides-horizontal": this.orientation === "horizontal",
      "slides-vertical": this.orientation === "vertical",
      "slides-dragging": this.dragging
    })}"
          style="--slides-per-page: ${this.slidesPerPage};"
          aria-busy="${scrolling ? "true" : "false"}"
          aria-atomic="true"
          tabindex="0"
          @keydown=${this.handleKeyDown}
          @mousedown="${this.handleMouseDragStart}"
          @scroll="${this.handleScroll}"
          @scrollend=${this.handleScrollEnd}
          @click=${this.handleClick}
        >
          <slot @slotchange=${() => this.requestUpdate()}></slot>
        </div>

        ${this.navigation ? x`
              <div part="navigation" class="navigation">
                <button
                  part="navigation-button navigation-button-previous"
                  class="${e2({
      "navigation-button": true,
      "navigation-button-previous": true,
      "navigation-button-disabled": !prevEnabled
    })}"
                  aria-label="${this.localize.term("previousSlide")}"
                  aria-controls="scroll-container"
                  aria-disabled="${prevEnabled ? "false" : "true"}"
                  @click=${prevEnabled ? () => this.previous() : null}
                >
                  <slot name="previous-icon">
                    <wa-icon library="system" name="${isRTL ? "chevron-right" : "chevron-left"}"></wa-icon>
                  </slot>
                </button>

                <button
                  part="navigation-button navigation-button-next"
                  class=${e2({
      "navigation-button": true,
      "navigation-button-next": true,
      "navigation-button-disabled": !nextEnabled
    })}
                  aria-label="${this.localize.term("nextSlide")}"
                  aria-controls="scroll-container"
                  aria-disabled="${nextEnabled ? "false" : "true"}"
                  @click=${nextEnabled ? () => this.next() : null}
                >
                  <slot name="next-icon">
                    <wa-icon library="system" name="${isRTL ? "chevron-left" : "chevron-right"}"></wa-icon>
                  </slot>
                </button>
              </div>
            ` : ""}
        ${this.pagination ? x`
              <div part="pagination" role="tablist" class="pagination" aria-controls="scroll-container">
                ${o2(o3(pagesCount), (index) => {
      const isActive = index === currentPage;
      return x`
                    <button
                      part="pagination-item ${isActive ? "pagination-item-active" : ""}"
                      class="${e2({
        "pagination-item": true,
        "pagination-item-active": isActive
      })}"
                      role="tab"
                      aria-selected="${isActive ? "true" : "false"}"
                      aria-label="${this.localize.term("goToSlide", index + 1, pagesCount)}"
                      tabindex=${isActive ? "0" : "-1"}
                      @click=${() => this.goToSlide(index * slidesPerMove)}
                      @keydown=${this.handleKeyDown}
                    ></button>
                  `;
    })}
              </div>
            ` : x``}
      </div>
    `;
  }
};
WaCarousel.css = carousel_default;
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaCarousel.prototype, "loop", 2);
__decorateClass([
  n({ type: Number, reflect: true })
], WaCarousel.prototype, "slides", 2);
__decorateClass([
  n({ type: Number, reflect: true })
], WaCarousel.prototype, "currentSlide", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaCarousel.prototype, "navigation", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaCarousel.prototype, "pagination", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaCarousel.prototype, "autoplay", 2);
__decorateClass([
  n({ type: Number, attribute: "autoplay-interval" })
], WaCarousel.prototype, "autoplayInterval", 2);
__decorateClass([
  n({ type: Number, attribute: "slides-per-page" })
], WaCarousel.prototype, "slidesPerPage", 2);
__decorateClass([
  n({ type: Number, attribute: "slides-per-move" })
], WaCarousel.prototype, "slidesPerMove", 2);
__decorateClass([
  n()
], WaCarousel.prototype, "orientation", 2);
__decorateClass([
  n({ type: Boolean, reflect: true, attribute: "mouse-dragging" })
], WaCarousel.prototype, "mouseDragging", 2);
__decorateClass([
  e(".slides")
], WaCarousel.prototype, "scrollContainer", 2);
__decorateClass([
  e(".pagination")
], WaCarousel.prototype, "paginationContainer", 2);
__decorateClass([
  r()
], WaCarousel.prototype, "activeSlide", 2);
__decorateClass([
  r()
], WaCarousel.prototype, "scrolling", 2);
__decorateClass([
  r()
], WaCarousel.prototype, "dragging", 2);
__decorateClass([
  t2({ passive: true })
], WaCarousel.prototype, "handleScroll", 1);
__decorateClass([
  watch("loop", { waitUntilFirstUpdate: true }),
  watch("slidesPerPage", { waitUntilFirstUpdate: true })
], WaCarousel.prototype, "initializeSlides", 1);
__decorateClass([
  watch("activeSlide")
], WaCarousel.prototype, "handleSlideChange", 1);
__decorateClass([
  watch("slidesPerMove")
], WaCarousel.prototype, "updateSlidesSnap", 1);
__decorateClass([
  watch("autoplay")
], WaCarousel.prototype, "handleAutoplayChange", 1);
WaCarousel = __decorateClass([
  t("wa-carousel")
], WaCarousel);

export {
  WaCarousel
};
/*! Bundled license information:

lit-html/directives/map.js:
lit-html/directives/range.js:
  (**
   * @license
   * Copyright 2021 Google LLC
   * SPDX-License-Identifier: BSD-3-Clause
   *)
*/
