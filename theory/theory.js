/* Shared behavior for theory look-up pages */
(function () {
  if (window.hljs) hljs.highlightAll();

  window.copyCode = function (btn) {
    const code = btn.closest(".code-wrap").querySelector("code").innerText;
    navigator.clipboard.writeText(code).then(function () {
      const original = btn.textContent;
      btn.textContent = "copied";
      setTimeout(function () { btn.textContent = original; }, 1200);
    });
  };

  const links = document.querySelectorAll("#toc-list a");
  if (!links.length) return;
  const sections = Array.from(links).map(function (l) {
    return document.querySelector(l.getAttribute("href"));
  });
  const observer = new IntersectionObserver(
    function (entries) {
      entries.forEach(function (entry) {
        const idx = sections.indexOf(entry.target);
        if (entry.isIntersecting && idx !== -1) {
          links.forEach(function (l) { l.classList.remove("active"); });
          links[idx].classList.add("active");
        }
      });
    },
    { rootMargin: "-20% 0px -70% 0px" }
  );
  sections.forEach(function (s) { if (s) observer.observe(s); });
})();
