/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */

// src/utilities/base-path.ts
var basePath = "";
var kitCode = "";
function setBasePath(path) {
  basePath = path;
}
function getBasePath(subpath = "") {
  if (!basePath) {
    const el = document.querySelector("[data-webawesome]");
    if (el?.hasAttribute("data-webawesome")) {
      const rootRelativeUrl = new URL(el.getAttribute("data-webawesome") ?? "", window.location.href).pathname;
      setBasePath(rootRelativeUrl);
    } else {
      const scripts = [...document.getElementsByTagName("script")];
      const waScript = scripts.find(
        (script) => script.src.endsWith("webawesome.js") || script.src.endsWith("webawesome.loader.js") || script.src.endsWith("webawesome.ssr-loader.js")
      );
      if (waScript) {
        const path = String(waScript.getAttribute("src"));
        setBasePath(path.split("/").slice(0, -1).join("/"));
      }
    }
  }
  return basePath.replace(/\/$/, "") + (subpath ? `/${subpath.replace(/^\//, "")}` : ``);
}
function setKitCode(code) {
  kitCode = code;
}
function getKitCode() {
  if (!kitCode) {
    const el = document.querySelector("[data-fa-kit-code]");
    if (el) {
      setKitCode(el.getAttribute("data-fa-kit-code") || "");
    }
  }
  return kitCode;
}

// src/components/icon/library.default.ts
var FA_VERSION = "7.0.1";
function getIconUrl(name, family, variant) {
  const kitCode2 = getKitCode();
  const isPro = kitCode2.length > 0;
  let folder = "solid";
  if (family === "notdog") {
    if (variant === "solid") folder = "solid";
    if (variant === "duo-solid") folder = "duo-solid";
    return `https://ka-p.fontawesome.com/releases/v${FA_VERSION}/svgs/notdog-${folder}/${name}.svg?token=${encodeURIComponent(kitCode2)}`;
  }
  if (family === "chisel") {
    return `https://ka-p.fontawesome.com/releases/v${FA_VERSION}/svgs/chisel-regular/${name}.svg?token=${encodeURIComponent(kitCode2)}`;
  }
  if (family === "etch") {
    return `https://ka-p.fontawesome.com/releases/v${FA_VERSION}/svgs/etch-solid/${name}.svg?token=${encodeURIComponent(kitCode2)}`;
  }
  if (family === "jelly") {
    if (variant === "regular") folder = "regular";
    if (variant === "duo-regular") folder = "duo-regular";
    if (variant === "fill-regular") folder = "fill-regular";
    return `https://ka-p.fontawesome.com/releases/v${FA_VERSION}/svgs/jelly-${folder}/${name}.svg?token=${encodeURIComponent(kitCode2)}`;
  }
  if (family === "slab") {
    if (variant === "solid" || variant === "regular") folder = "regular";
    if (variant === "press-regular") folder = "press-regular";
    return `https://ka-p.fontawesome.com/releases/v${FA_VERSION}/svgs/slab-${folder}/${name}.svg?token=${encodeURIComponent(kitCode2)}`;
  }
  if (family === "thumbprint") {
    return `https://ka-p.fontawesome.com/releases/v${FA_VERSION}/svgs/thumbprint-light/${name}.svg?token=${encodeURIComponent(kitCode2)}`;
  }
  if (family === "whiteboard") {
    return `https://ka-p.fontawesome.com/releases/v${FA_VERSION}/svgs/whiteboard-semibold/${name}.svg?token=${encodeURIComponent(kitCode2)}`;
  }
  if (family === "classic") {
    if (variant === "thin") folder = "thin";
    if (variant === "light") folder = "light";
    if (variant === "regular") folder = "regular";
    if (variant === "solid") folder = "solid";
  }
  if (family === "sharp") {
    if (variant === "thin") folder = "sharp-thin";
    if (variant === "light") folder = "sharp-light";
    if (variant === "regular") folder = "sharp-regular";
    if (variant === "solid") folder = "sharp-solid";
  }
  if (family === "duotone") {
    if (variant === "thin") folder = "duotone-thin";
    if (variant === "light") folder = "duotone-light";
    if (variant === "regular") folder = "duotone-regular";
    if (variant === "solid") folder = "duotone";
  }
  if (family === "sharp-duotone") {
    if (variant === "thin") folder = "sharp-duotone-thin";
    if (variant === "light") folder = "sharp-duotone-light";
    if (variant === "regular") folder = "sharp-duotone-regular";
    if (variant === "solid") folder = "sharp-duotone-solid";
  }
  if (family === "brands") {
    folder = "brands";
  }
  return isPro ? `https://ka-p.fontawesome.com/releases/v${FA_VERSION}/svgs/${folder}/${name}.svg?token=${encodeURIComponent(kitCode2)}` : `https://ka-f.fontawesome.com/releases/v${FA_VERSION}/svgs/${folder}/${name}.svg`;
}
var library = {
  name: "default",
  resolver: (name, family = "classic", variant = "solid") => {
    return getIconUrl(name, family, variant);
  },
  mutator: (svg, hostEl) => {
    if (hostEl?.family && !svg.hasAttribute("data-duotone-initialized")) {
      const { family, variant } = hostEl;
      if (
        // Duotone
        family === "duotone" || // Sharp duotone
        family === "sharp-duotone" || // Notdog duo-solid
        family === "notdog" && variant === "duo-solid" || // Jelly duo-regular
        family === "jelly" && variant === "duo-regular" || // Thumbprint
        family === "thumbprint"
      ) {
        const paths = [...svg.querySelectorAll("path")];
        const primaryPath = paths.find((p) => !p.hasAttribute("opacity"));
        const secondaryPath = paths.find((p) => p.hasAttribute("opacity"));
        if (!primaryPath || !secondaryPath) return;
        primaryPath.setAttribute("data-duotone-primary", "");
        secondaryPath.setAttribute("data-duotone-secondary", "");
        if (hostEl.swapOpacity && primaryPath && secondaryPath) {
          const originalOpacity = secondaryPath.getAttribute("opacity") || "0.4";
          primaryPath.style.setProperty("--path-opacity", originalOpacity);
          secondaryPath.style.setProperty("--path-opacity", "1");
        }
        svg.setAttribute("data-duotone-initialized", "");
      }
    }
  }
};
var library_default_default = library;

export {
  setBasePath,
  getBasePath,
  setKitCode,
  getKitCode,
  library_default_default
};
