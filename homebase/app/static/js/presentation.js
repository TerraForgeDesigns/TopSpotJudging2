// Awards presentation mode. All slide markup is server-rendered up front
// (see awards/present.html) — this script only toggles visibility classes
// and reads per-slide data from data-* attributes already in the DOM, so
// there's no fetch, no template rendering, and nothing that can spinner
// or flash in front of an audience.
(function () {
  const root = document.querySelector('.pres-root');
  const startScreen = document.querySelector('.pres-start');
  const startButton = document.querySelector('[data-pres-start]');
  const slideEls = Array.from(document.querySelectorAll('.pres-slide'));
  const prevBtn = document.querySelector('[data-pres-prev]');
  const nextBtn = document.querySelector('[data-pres-next]');
  const progressText = document.querySelector('[data-pres-progress-text]');
  const progressDots = Array.from(document.querySelectorAll('.pres-progress__dot'));
  const sheetOverlay = document.querySelector('.pres-sheet-overlay');
  const sheetImg = sheetOverlay ? sheetOverlay.querySelector('img') : null;

  if (!root || slideEls.length === 0) return;

  let currentIndex = 0;
  const revealed = slideEls.map(() => false);
  let sheetVisible = false;
  let controlsTimer = null;

  function preloadImage(url) {
    if (!url) return;
    const img = new Image();
    img.src = url;
  }

  // Every slide's <img> already sits in the DOM from first paint (opacity
  // only, never display:none), so the browser has already started fetching
  // all of them — this is defensive/idempotent, not the primary mechanism.
  function preloadAround(index) {
    [index - 1, index, index + 1].forEach((i) => {
      const el = slideEls[i];
      if (!el) return;
      preloadImage(el.dataset.carPhotoUrl);
      preloadImage(el.dataset.judgeSheetUrl);
    });
  }

  function updateProgress() {
    if (progressText) progressText.textContent = (currentIndex + 1) + ' / ' + slideEls.length;
    progressDots.forEach((dot, i) => {
      dot.classList.toggle('is-current', i === currentIndex);
      dot.classList.toggle('is-done', i < currentIndex);
    });
  }

  function showSlide(index) {
    if (index < 0 || index >= slideEls.length) return;
    currentIndex = index;
    slideEls.forEach((el, i) => {
      el.classList.toggle('is-active', i === index);
      el.classList.toggle('is-revealed', revealed[i]);
    });
    if (prevBtn) prevBtn.disabled = index === 0;
    updateProgress();
    preloadAround(index);
    if (sheetVisible) toggleSheet(false);
  }

  function reveal() {
    if (revealed[currentIndex]) return;
    revealed[currentIndex] = true;
    slideEls[currentIndex].classList.add('is-revealed');
  }

  function advance() {
    if (!revealed[currentIndex]) {
      reveal();
      return;
    }
    showSlide(currentIndex + 1);
  }

  function retreat() {
    showSlide(currentIndex - 1);
  }

  function toggleSheet(forceState) {
    const el = slideEls[currentIndex];
    const url = el ? el.dataset.judgeSheetUrl : '';
    if (!url) return;
    sheetVisible = typeof forceState === 'boolean' ? forceState : !sheetVisible;
    if (sheetOverlay) sheetOverlay.classList.toggle('is-visible', sheetVisible);
    if (sheetImg && sheetVisible) sheetImg.src = url;
  }

  function showControls() {
    root.classList.add('controls-visible');
    if (controlsTimer) clearTimeout(controlsTimer);
    controlsTimer = setTimeout(() => root.classList.remove('controls-visible'), 3000);
  }

  document.addEventListener('mousemove', showControls);

  if (nextBtn) nextBtn.addEventListener('click', advance);
  if (prevBtn) prevBtn.addEventListener('click', retreat);

  document.addEventListener('keydown', (event) => {
    if (startScreen && !startScreen.classList.contains('is-hidden')) return;

    if (event.key === 'Escape') {
      event.preventDefault();
      if (sheetVisible) {
        toggleSheet(false);
        return;
      }
      if (document.fullscreenElement) {
        document.exitFullscreen();
      } else {
        window.location.href = '/awards';
      }
      return;
    }
    if (event.key === 'ArrowRight' || event.key === ' ') {
      event.preventDefault();
      advance();
    } else if (event.key === 'ArrowLeft') {
      event.preventDefault();
      retreat();
    } else if (event.key === 'j' || event.key === 'J') {
      toggleSheet();
    }
  });

  // Covers both our own Escape handler exiting fullscreen AND the browser's
  // own native fullscreen-exit-on-Escape — either way, leave presentation.
  document.addEventListener('fullscreenchange', () => {
    const started = startScreen && startScreen.classList.contains('is-hidden');
    if (!document.fullscreenElement && started) {
      window.location.href = '/awards';
    }
  });

  if (startButton) {
    startButton.addEventListener('click', () => {
      const el = document.documentElement;
      if (el.requestFullscreen) {
        el.requestFullscreen().catch(() => {});
      }
      if (startScreen) startScreen.classList.add('is-hidden');
      showSlide(0);
      showControls();
    });
  }
})();
