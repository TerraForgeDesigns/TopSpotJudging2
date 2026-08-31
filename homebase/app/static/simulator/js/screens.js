// Screen stack — mirrors ui/screen_manager.h's exact model: only one
// screen's DOM subtree exists at a time (pushing tears down the current
// one immediately, matching the firmware's "screens must create and
// destroy cleanly" rule, not a hide/show toggle), popping rebuilds the
// previous screen fresh from its recorded (factory, arg) pair, and a
// persistent status bar + header (title + Back, hidden at the root)
// survive every screen change untouched.
//
// A "screen" here is a plain function: (content, arg) => (teardownFn | undefined).
// `content` is the DOM node to build into; the returned function (if any)
// is called right before the screen is torn down, for the same reason
// PhotosScreen::teardown() exists in firmware — releasing anything not
// owned by the DOM subtree itself (a running timer, an object URL).

const MAX_DEPTH = 8;

let stack = [];
let currentTeardown = null;

let elHeader, elBackBtn, elHeaderTitle, elContent;

export function initScreenManager({ header, backBtn, headerTitle, content }) {
  elHeader = header;
  elBackBtn = backBtn;
  elHeaderTitle = headerTitle;
  elContent = content;
  elBackBtn.addEventListener("click", pop);
}

function destroyCurrent() {
  if (currentTeardown) {
    currentTeardown();
    currentTeardown = null;
  }
  elContent.innerHTML = "";
}

function buildCurrent() {
  const top = stack[stack.length - 1];
  elHeaderTitle.textContent = top.title;
  elBackBtn.hidden = stack.length <= 1;
  currentTeardown = top.factory(elContent, top.arg) || null;
}

export function push(factory, arg, title) {
  if (stack.length >= MAX_DEPTH) return; // never crash on a runaway push chain — just stop navigating deeper
  destroyCurrent();
  stack.push({ factory, arg, title: title || "" });
  buildCurrent();
}

export function pop() {
  if (stack.length <= 1) return; // can't pop the root
  destroyCurrent();
  stack.pop();
  buildCurrent();
}

export function popToRoot() {
  if (stack.length <= 1) return;
  destroyCurrent();
  stack.length = 1;
  buildCurrent();
}

export function refresh() {
  destroyCurrent();
  buildCurrent();
}

export function depth() {
  return stack.length;
}
