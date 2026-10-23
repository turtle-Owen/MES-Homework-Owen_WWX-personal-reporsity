"use strict";

Object.defineProperty(exports, "__esModule", {
  value: true
});
exports["default"] = DateType;
var _InvalidError = _interopRequireDefault(require("../InvalidError.js"));
function _interopRequireDefault(obj) { return obj && obj.__esModule ? obj : { "default": obj }; }
function DateType(value) {
  // XLSX has no specific format for dates.
  // Sometimes a date can be heuristically detected.
  // https://github.com/catamphetamine/read-excel-file/issues/3#issuecomment-395770777
  if (value instanceof Date) {
    if (isNaN(value.valueOf())) {
      throw new _InvalidError["default"]('out_of_bounds');
    }
    return value;
  }
  throw new _InvalidError["default"]('not_a_date');
}
//# sourceMappingURL=Date.js.map