// Click a screenshot to see it full size.
(function () {
  var box = document.getElementById('lightbox');
  if (!box || typeof box.showModal !== 'function') return;
  var big = box.querySelector('img');
  document.querySelectorAll('.window img').forEach(function (img) {
    img.addEventListener('click', function () {
      big.src = img.currentSrc || img.src;
      big.alt = img.alt;
      box.showModal();
    });
  });
  box.addEventListener('click', function () { box.close(); });
})();
