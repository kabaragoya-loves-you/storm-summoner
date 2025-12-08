/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */
import {
  getBasePath
} from "./chunk.I3WWI2TE.js";

// src/utilities/autoloader.ts
var observer = new MutationObserver((mutations) => {
  for (const { addedNodes } of mutations) {
    for (const node of addedNodes) {
      if (node.nodeType === Node.ELEMENT_NODE) {
        discover(node);
      }
    }
  }
});
function startLoader() {
  discover(document);
  observer.observe(document.documentElement, { subtree: true, childList: true });
}
function stopLoader() {
  observer.disconnect();
}
async function discover(root) {
  const rootTagName = root instanceof Element ? root.tagName.toLowerCase() : "";
  const rootIsWebAwesomeComponent = rootTagName?.startsWith("wa-");
  const tags = [...root.querySelectorAll(":not(:defined)")].map((el) => el.tagName.toLowerCase()).filter((tag) => tag.startsWith("wa-"));
  if (rootIsWebAwesomeComponent && !customElements.get(rootTagName)) {
    tags.push(rootTagName);
  }
  const tagsToRegister = [...new Set(tags)];
  const imports = await Promise.allSettled(tagsToRegister.map((tagName) => register(tagName)));
  for (const imp of imports) {
    if (imp.status === "rejected") {
      console.warn(imp.reason);
    }
  }
  await new Promise(requestAnimationFrame);
  root.dispatchEvent(
    new CustomEvent("wa-discovery-complete", {
      bubbles: false,
      cancelable: false,
      composed: true
    })
  );
}
function register(tagName) {
  if (customElements.get(tagName)) {
    return Promise.resolve();
  }
  const tagWithoutPrefix = tagName.replace(/^wa-/i, "");
  const path = getBasePath(`components/${tagWithoutPrefix}/${tagWithoutPrefix}.js`);
  return new Promise((resolve, reject) => {
    import(path).then(() => resolve()).catch(() => reject(new Error(`Unable to autoload <${tagName}> from ${path}`)));
  });
}
function preventTurboFouce(timeout = 2e3) {
  document.addEventListener("turbo:before-render", async (event) => {
    const newBody = event.detail.newBody;
    event.preventDefault();
    try {
      await Promise.race([discover(newBody), new Promise((resolve) => setTimeout(resolve, timeout))]);
    } finally {
      event.detail.resume();
    }
  });
}

// src/utilities/defined.ts
async function allDefined(options) {
  const opts = {
    match: (tagName) => tagName.startsWith("wa-"),
    additionalElements: [],
    root: document,
    ...options
  };
  const additionalElements = Array.isArray(opts.additionalElements) ? opts.additionalElements : [opts.additionalElements];
  const undefinedElements = [...opts.root.querySelectorAll(":not(:defined)")].map((el) => el.localName).filter((tag, index, arr) => arr.indexOf(tag) === index).filter((tag) => opts.match(tag));
  const tagsToAwait = [...undefinedElements, ...additionalElements];
  await Promise.all(tagsToAwait.map((tag) => customElements.whenDefined(tag)));
  await new Promise(requestAnimationFrame);
}

// src/utilities/form.ts
function serialize(form) {
  const formData = new FormData(form);
  const object = {};
  formData.forEach((value, key) => {
    if (Reflect.has(object, key)) {
      const entry = object[key];
      if (Array.isArray(entry)) {
        entry.push(value);
      } else {
        object[key] = [object[key], value];
      }
    } else {
      object[key] = value;
    }
  });
  return object;
}

export {
  startLoader,
  stopLoader,
  discover,
  preventTurboFouce,
  allDefined,
  serialize
};
