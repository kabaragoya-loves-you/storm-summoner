/*! Copyright 2025 Fonticons, Inc. - https://webawesome.com/license */

// src/styles/component/form-control.css
var form_control_default = ":host {\n  display: flex;\n  flex-direction: column;\n}\n\n/* Label */\n:is([part~='form-control-label'], [part~='label']):has(*:not(:empty)) {\n  display: inline-flex;\n  color: var(--wa-form-control-label-color);\n  font-weight: var(--wa-form-control-label-font-weight);\n  line-height: var(--wa-form-control-label-line-height);\n  margin-block-end: 0.5em;\n}\n\n:host([required]) :is([part~='form-control-label'], [part~='label'])::after {\n  content: var(--wa-form-control-required-content);\n  margin-inline-start: var(--wa-form-control-required-content-offset);\n  color: var(--wa-form-control-required-content-color);\n}\n\n/* Help text */\n[part~='hint'] {\n  display: block;\n  color: var(--wa-form-control-hint-color);\n  font-weight: var(--wa-form-control-hint-font-weight);\n  line-height: var(--wa-form-control-hint-line-height);\n  margin-block-start: 0.5em;\n  font-size: var(--wa-font-size-smaller);\n  line-height: var(--wa-form-control-label-line-height);\n\n  &:not(.has-slotted) {\n    display: none;\n  }\n}\n";

export {
  form_control_default
};
