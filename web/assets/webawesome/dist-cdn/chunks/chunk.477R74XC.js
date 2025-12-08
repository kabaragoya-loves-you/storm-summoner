/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  LocalizeController
} from "./chunk.DR3YY3XN.js";
import {
  WaErrorEvent
} from "./chunk.F3GLEBRH.js";
import {
  WaLoadEvent
} from "./chunk.YNHANM3W.js";
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

// src/components/animated-image/animated-image.css
var animated_image_default = ":host {\n  --control-box-size: 3rem;\n  --icon-size: calc(var(--control-box-size) * 0.625);\n\n  display: inline-flex;\n  position: relative;\n  cursor: pointer;\n}\n\nimg {\n  display: block;\n  width: 100%;\n  height: 100%;\n}\n\nimg[aria-hidden='true'] {\n  display: none;\n}\n\n.control-box {\n  display: flex;\n  position: absolute;\n  align-items: center;\n  justify-content: center;\n  top: calc(50% - var(--control-box-size) / 2);\n  right: calc(50% - var(--control-box-size) / 2);\n  width: var(--control-box-size);\n  height: var(--control-box-size);\n  font-size: calc(var(--icon-size) * 0.75);\n  background: none;\n  border: solid var(--wa-border-width-s) currentColor;\n  background-color: rgb(0 0 0 / 50%);\n  border-radius: var(--wa-border-radius-circle);\n  color: white;\n  pointer-events: none;\n  transition: opacity var(--wa-transition-normal) var(--wa-transition-easing);\n}\n\n@media (hover: hover) {\n  :host([play]:hover) .control-box {\n    opacity: 1;\n  }\n}\n\n:where(:host([play]:not(:hover))) .control-box {\n  opacity: 0;\n}\n\n:host([play]) slot[name='play-icon'],\n:host(:not([play])) slot[name='pause-icon'] {\n  display: none;\n}\n\n/* Show control box on keyboard focus */\n.animated-image {\n  &:focus {\n    outline: none;\n  }\n\n  &:focus-visible .control-box {\n    opacity: 1;\n    outline: var(--wa-focus-ring);\n    outline-offset: var(--wa-focus-ring-offset);\n  }\n}\n";

// src/components/animated-image/animated-image.ts
var WaAnimatedImage = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.localize = new LocalizeController(this);
    this.isLoaded = false;
  }
  handleClick() {
    this.play = !this.play;
  }
  handleKeyDown(event) {
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      this.play = !this.play;
    }
  }
  handleLoad() {
    const canvas = document.createElement("canvas");
    const { width, height } = this.animatedImage;
    canvas.width = width;
    canvas.height = height;
    canvas.getContext("2d").drawImage(this.animatedImage, 0, 0, width, height);
    this.frozenFrame = canvas.toDataURL("image/gif");
    if (!this.isLoaded) {
      this.dispatchEvent(new WaLoadEvent());
      this.isLoaded = true;
    }
  }
  handleError() {
    this.dispatchEvent(new WaErrorEvent());
  }
  handlePlayChange() {
    if (this.play) {
      this.animatedImage.src = "";
      this.animatedImage.src = this.src;
    }
  }
  handleSrcChange() {
    this.isLoaded = false;
  }
  render() {
    const verb = this.localize.term(this.play ? "pauseAnimation" : "playAnimation");
    const label = `${verb} ${this.alt}`;
    return x`
      <div
        class="animated-image"
        tabindex="0"
        role="button"
        aria-pressed=${this.play ? "true" : "false"}
        aria-label=${label}
        @click=${this.handleClick}
        @keydown=${this.handleKeyDown}
      >
        <img
          class="animated"
          src=${this.src}
          alt=${this.alt}
          crossorigin="anonymous"
          aria-hidden=${this.play ? "false" : "true"}
          role="presentation"
          @load=${this.handleLoad}
          @error=${this.handleError}
        />

        ${this.isLoaded ? x`
              <img
                class="frozen"
                src=${this.frozenFrame}
                alt=${this.alt}
                aria-hidden=${this.play ? "true" : "false"}
                role="presentation"
              />

              <div part="control-box" class="control-box" aria-hidden="true">
                <slot name="play-icon">
                  <wa-icon
                    name="play"
                    library="system"
                    variant="solid"
                    class="default"
                    style="margin-inline-start: 3px;"
                  ></wa-icon>
                </slot>
                <slot name="pause-icon">
                  <wa-icon name="pause" library="system" variant="solid" class="default"></wa-icon>
                </slot>
              </div>
            ` : ""}
      </div>
    `;
  }
};
WaAnimatedImage.css = animated_image_default;
__decorateClass([
  e(".animated")
], WaAnimatedImage.prototype, "animatedImage", 2);
__decorateClass([
  r()
], WaAnimatedImage.prototype, "frozenFrame", 2);
__decorateClass([
  r()
], WaAnimatedImage.prototype, "isLoaded", 2);
__decorateClass([
  n()
], WaAnimatedImage.prototype, "src", 2);
__decorateClass([
  n()
], WaAnimatedImage.prototype, "alt", 2);
__decorateClass([
  n({ type: Boolean, reflect: true })
], WaAnimatedImage.prototype, "play", 2);
__decorateClass([
  watch("play", { waitUntilFirstUpdate: true })
], WaAnimatedImage.prototype, "handlePlayChange", 1);
__decorateClass([
  watch("src")
], WaAnimatedImage.prototype, "handleSrcChange", 1);
WaAnimatedImage = __decorateClass([
  t("wa-animated-image")
], WaAnimatedImage);

export {
  WaAnimatedImage
};
