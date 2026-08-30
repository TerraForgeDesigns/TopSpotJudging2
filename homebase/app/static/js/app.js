// Small vanilla-JS helpers. No framework, no build step — see /README.md.

document.addEventListener("click", (event) => {
  const opener = event.target.closest("[data-modal-open]");
  if (opener) {
    const modal = document.getElementById(opener.dataset.modalOpen);
    if (modal) modal.hidden = false;
  }

  const closer = event.target.closest("[data-modal-close]");
  if (closer) {
    const modal = closer.closest(".modal-overlay");
    if (modal) modal.hidden = true;
  }

  const dismiss = event.target.closest("[data-toast-dismiss]");
  if (dismiss) {
    const toast = dismiss.closest(".toast");
    if (toast) toast.remove();
  }
});
