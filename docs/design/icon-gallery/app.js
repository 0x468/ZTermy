(() => {
  const data = window.ZtermyIconData;
  const families = {
    rounded: { label: "Rounded", stroke: 1.5, cap: "round", join: "round" },
    compact: { label: "Compact", stroke: 1.8, cap: "round", join: "round" },
    precision: { label: "Precision", stroke: 1.25, cap: "square", join: "miter" }
  };
  const state = {
    family: "rounded",
    size: 20,
    category: "chrome",
    query: "",
    lockupContext: "main",
    selected: new Set(JSON.parse(localStorage.getItem("ztermy-icon-selection") || "[]")),
    preview: null
  };

  const elements = {
    brandGrid: document.querySelector("#brandGrid"),
    brandSection: document.querySelector("#brandSection"),
    lockupSection: document.querySelector("#lockupSection"),
    arrowReviewSection: document.querySelector("#arrowReviewSection"),
    iconSection: document.querySelector("#iconSection"),
    iconGrid: document.querySelector("#iconGrid"),
    search: document.querySelector("#searchInput"),
    family: document.querySelector("#familyControl"),
    size: document.querySelector("#sizeSelect"),
    theme: document.querySelector("#themeButton"),
    themeLabel: document.querySelector("#themeLabel"),
    categories: document.querySelector("#categoryList"),
    lockupContext: document.querySelector("#lockupContextControl"),
    lockupCanvas: document.querySelector("#lockupCanvas"),
    lockupSpecs: document.querySelector("#lockupSpecs"),
    lockupDialog: document.querySelector("#lockupDialog"),
    lockupDialogTitle: document.querySelector("#lockupDialogTitle"),
    lockupDialogPreview: document.querySelector("#lockupDialogPreview"),
    arrowReview: document.querySelector("#arrowReviewGrid"),
    resultSummary: document.querySelector("#resultSummary"),
    galleryTitle: document.querySelector("#galleryTitle"),
    selectedCount: document.querySelector("#selectedCount"),
    export: document.querySelector("#exportButton"),
    empty: document.querySelector("#emptyState"),
    dialog: document.querySelector("#previewDialog"),
    dialogCategory: document.querySelector("#dialogCategory"),
    dialogTitle: document.querySelector("#dialogTitle"),
    dialogDescription: document.querySelector("#dialogDescription"),
    comparison: document.querySelector("#comparisonGrid"),
    sizeStrip: document.querySelector("#sizeStrip"),
    copySvg: document.querySelector("#copySvgButton"),
    selectDialog: document.querySelector("#selectDialogButton"),
    toast: document.querySelector("#toast")
  };

  function svgMarkup(item, familyName = state.family, size = state.size, className = "") {
    const family = families[familyName];
    return `<svg class="${className}" viewBox="0 0 20 20" width="${size}" height="${size}" fill="none" stroke="currentColor" stroke-width="${family.stroke}" stroke-linecap="${family.cap}" stroke-linejoin="${family.join}" xmlns="http://www.w3.org/2000/svg" aria-hidden="true">${item.body}</svg>`;
  }

  function matches(item) {
    if (state.category !== "all" && item.category !== state.category) return false;
    if (!state.query) return true;
    const haystack = [item.id, item.name, item.zh, item.category, ...item.tags].join(" ").toLowerCase();
    return haystack.includes(state.query);
  }

  function isSelected(item) { return state.selected.has(item.id); }

  function brandMarkMarkup(x, y, size, gradientId) {
    const scale = size / 18;
    const translateX = x - scale;
    const translateY = y - scale;
    return `<g transform="translate(${translateX} ${translateY}) scale(${scale})">
      <defs><linearGradient id="${gradientId}" x1="4" y1="4" x2="15" y2="15" gradientUnits="userSpaceOnUse"><stop offset="0" stop-color="#7de7ff"/><stop offset="1" stop-color="#3c89ff"/></linearGradient></defs>
      <rect x="1" y="1" width="18" height="18" rx="4.2" fill="#102544"/>
      <path d="M5.55 4.85H15.85L14.45 6.75H9.75L6.75 13.25H15.55L14.15 15.15H4.15L7.15 8.65H11.55L12.95 6.75H4.75Z" fill="url(#${gradientId})"/>
      <path d="M9.2 9.85 10.5 11 9.2 12.15M11.2 12.15H13.2" fill="none" stroke="#e9f7ff" stroke-linecap="round" stroke-linejoin="round" stroke-width=".64"/>
    </g>`;
  }

  function lockupMarkup(context, suffix) {
    const gradientId = `lockup-gradient-${suffix}`;
    if (context === "titlebar") {
      return `<svg viewBox="0 0 1400 160" role="img" aria-label="ztermy 标题栏紧凑字标" xmlns="http://www.w3.org/2000/svg">
        ${brandMarkMarkup(68.4, 24.64, 109.12, gradientId)}
        <text x="218" y="108" font-family="Segoe UI Variable Display, Segoe UI, Arial, sans-serif" font-size="88" font-weight="700"><tspan fill="#55c8ff">Z</tspan><tspan fill="#f8fafc">termy</tspan></text>
      </svg>`;
    }
    if (context === "about") {
      return `<svg viewBox="0 0 1400 420" role="img" aria-label="ztermy 关于页面品牌横幅" xmlns="http://www.w3.org/2000/svg">
        <rect width="1400" height="420" fill="#eaf0f7"/><rect x="54" y="42" width="1292" height="336" rx="38" fill="#fff" stroke="#d9e3ee"/>
        ${brandMarkMarkup(130, 84, 248, gradientId)}
        <text x="860" y="182" text-anchor="middle" font-family="Segoe UI Variable Display, Segoe UI, Arial, sans-serif" font-size="122" font-weight="700"><tspan fill="#2aa8ff">Z</tspan><tspan fill="#0f172a">termy</tspan></text>
        <text x="860" y="248" text-anchor="middle" font-family="Segoe UI Variable Text, Segoe UI, Arial, sans-serif" font-size="31" font-weight="600" letter-spacing="9" fill="#64748b">SSH TERMINAL</text>
        <text x="860" y="307" text-anchor="middle" font-family="Segoe UI Variable Text, Segoe UI, Arial, sans-serif" font-size="22" fill="#94a3b8">Native · Focused · Windows 11 first</text>
      </svg>`;
    }
    return `<svg viewBox="0 0 1400 360" role="img" aria-label="ztermy 主页面横向品牌标识" xmlns="http://www.w3.org/2000/svg">
      <rect width="1400" height="360" fill="#fff"/>
      ${brandMarkMarkup(110, 56, 248, gradientId)}
      <text x="880" y="226" text-anchor="middle" font-family="Segoe UI Variable Display, Segoe UI, Arial, sans-serif" font-size="126" font-weight="700"><tspan fill="#2aa8ff">Z</tspan><tspan fill="#0f172a">termy</tspan></text>
      <text x="880" y="282" text-anchor="middle" font-family="Segoe UI Variable Text, Segoe UI, Arial, sans-serif" font-size="34" font-weight="600" letter-spacing="9" fill="#64748b">SSH TERMINAL</text>
    </svg>`;
  }

  function renderLockup() {
    const context = state.lockupContext;
    const contextClass = context === "titlebar" ? "titlebar-preview" : context === "about" ? "about-preview" : "";
    const titles = { main: "主页面横幅", titlebar: "标题栏紧凑字标", about: "关于页面品牌卡" };
    const specs = {
      main: ["1400 × 360", "35:9", "完整副标题", "已确认品牌图形"],
      titlebar: ["1400 × 160", "紧凑横排", "省略副标题", "适合 32–44px 高度"],
      about: ["1400 × 420", "卡片容器", "允许版本信息", "三行整体居中"]
    };
    elements.lockupCanvas.className = `lockup-canvas ${contextClass}`.trim();
    elements.lockupCanvas.innerHTML = lockupMarkup(context, "canvas");
    elements.lockupSpecs.innerHTML = specs[context].map(spec => `<span>${spec}</span>`).join("");
    elements.lockupDialogTitle.textContent = titles[context];
    elements.lockupDialogPreview.className = `lockup-dialog-stage ${contextClass}`.trim();
    elements.lockupDialogPreview.innerHTML = lockupMarkup(context, "dialog");
    elements.lockupContext.querySelectorAll("button[data-lockup-context]").forEach(button => button.setAttribute("aria-pressed", button.dataset.lockupContext === context ? "true" : "false"));
  }

  function persistSelection() {
    localStorage.setItem("ztermy-icon-selection", JSON.stringify([...state.selected]));
    elements.selectedCount.textContent = state.selected.size;
  }

  function toggleSelection(item) {
    if (isSelected(item)) state.selected.delete(item.id);
    else state.selected.add(item.id);
    persistSelection();
    render();
    if (state.preview?.id === item.id) updateDialogSelection();
  }

  function cardButton(item, brand = false) {
    const button = document.createElement("button");
    const approved = brand && item.id === data.approvedBrandId;
    button.type = "button";
    button.className = `${brand ? "brand-card" : "icon-card"}${isSelected(item) ? " selected" : ""}${approved ? " approved" : ""}`;
    button.setAttribute("aria-label", `预览 ${item.zh}，${item.name}${approved ? "，已选品牌基线" : ""}`);
    button.innerHTML = brand
      ? `${approved ? '<span class="approved-badge">已选基线</span>' : ""}<span class="select-dot">✓</span><div class="brand-preview">${svgMarkup(item, "rounded", 96)}</div><div class="brand-meta"><div><h3>${item.zh}</h3><p>${item.name}</p></div><code>${item.id.replace("brand-", "")}</code></div>`
      : `<span class="select-dot">✓</span><div class="icon-stage">${svgMarkup(item)}</div><div class="icon-meta"><h3>${item.zh}</h3><p>${item.name}</p><code>${item.id}</code></div>`;
    button.addEventListener("click", () => openPreview(item));
    return button;
  }

  function renderCategories() {
    elements.categories.replaceChildren(...data.categories.map(category => {
      const button = document.createElement("button");
      button.type = "button";
      button.textContent = category.label;
      button.dataset.category = category.id;
      button.setAttribute("aria-pressed", state.category === category.id ? "true" : "false");
      button.addEventListener("click", () => {
        state.category = category.id;
        render();
      });
      return button;
    }));
  }

  function render() {
    const visibleIcons = data.icons.filter(matches);
    const brandVisible = state.category === "all" && !state.query;
    elements.lockupSection.hidden = !brandVisible;
    elements.arrowReviewSection.hidden = !brandVisible;
    elements.brandSection.hidden = !brandVisible;
    elements.brandGrid.replaceChildren(...data.brand.map(item => cardButton(item, true)));
    elements.iconGrid.replaceChildren(...visibleIcons.map(item => cardButton(item)));
    elements.empty.hidden = visibleIcons.length > 0;
    const category = data.categories.find(item => item.id === state.category);
    elements.galleryTitle.textContent = state.category === "all" ? "全部功能图标" : `${category.label} 图标`;
    elements.resultSummary.textContent = `显示 ${visibleIcons.length} / ${data.icons.length} · ${families[state.family].label} · ${state.size}px`;
    document.documentElement.style.setProperty("--icon-size", `${state.size}px`);
    const arrowIds = ["terminal-history", "refresh", "retry", "vault-portable", "reset"];
    elements.arrowReview.replaceChildren(...arrowIds.map(findItem).filter(Boolean).map(item => {
      const button = cardButton(item);
      button.classList.add("arrow-review-card");
      return button;
    }));
    renderCategories();
    persistSelection();
  }

  function findItem(id) {
    return [...data.brand, ...data.icons].find(item => item.id === id);
  }

  function openPreview(item) {
    state.preview = item;
    elements.dialogCategory.textContent = data.categoryLabels[item.category].toUpperCase();
    elements.dialogTitle.textContent = `${item.zh} · ${item.name}`;
    elements.dialogDescription.textContent = `资源名：${item.id}。关键词：${item.tags.join("、") || "无"}。下方可以同时比较三种统一笔触，而不是孤立挑选单个按钮。`;
    elements.comparison.innerHTML = Object.entries(families).map(([id, family]) => `<div class="comparison-card"><div class="comparison-stage">${svgMarkup(item, id, 88)}</div><h3>${family.label}</h3><p>${family.stroke}px · ${family.cap} cap</p></div>`).join("");
    elements.sizeStrip.innerHTML = [16, 20, 24, 32, 48].map(size => `<div class="size-sample">${svgMarkup(item, state.family, size)}<span>${size}px</span></div>`).join("");
    updateDialogSelection();
    elements.dialog.showModal();
  }

  function updateDialogSelection() {
    if (!state.preview) return;
    const selected = isSelected(state.preview);
    elements.selectDialog.textContent = selected ? "移出候选" : "加入候选";
  }

  function showToast(message) {
    elements.toast.textContent = message;
    elements.toast.classList.add("visible");
    clearTimeout(showToast.timer);
    showToast.timer = setTimeout(() => elements.toast.classList.remove("visible"), 1800);
  }

  async function copyCurrentSvg() {
    if (!state.preview) return;
    try {
      await navigator.clipboard.writeText(svgMarkup(state.preview, state.family, 20));
      showToast("已复制当前 SVG");
    } catch {
      showToast("浏览器未允许写入剪贴板");
    }
  }

  function exportSelection() {
    const selected = [...state.selected].map(findItem).filter(Boolean).map(item => ({ id: item.id, name: item.name, zh: item.zh, category: item.category }));
    if (!selected.length) {
      showToast("请先加入至少一个候选");
      return;
    }
    const blob = new Blob([JSON.stringify({ project: "ztermy", family: state.family, size: state.size, selected }, null, 2)], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = "ztermy-icon-selection.json";
    anchor.click();
    URL.revokeObjectURL(url);
    showToast(`已导出 ${selected.length} 个候选`);
  }

  elements.search.addEventListener("input", event => {
    state.query = event.target.value.trim().toLowerCase();
    render();
  });
  elements.lockupContext.addEventListener("click", event => {
    const button = event.target.closest("button[data-lockup-context]");
    if (!button) return;
    state.lockupContext = button.dataset.lockupContext;
    renderLockup();
  });
  elements.lockupCanvas.addEventListener("click", () => elements.lockupDialog.showModal());
  elements.family.addEventListener("click", event => {
    const button = event.target.closest("button[data-family]");
    if (!button) return;
    state.family = button.dataset.family;
    elements.family.querySelectorAll("button").forEach(item => item.setAttribute("aria-pressed", item === button ? "true" : "false"));
    render();
  });
  elements.size.addEventListener("change", event => {
    state.size = Number(event.target.value);
    render();
  });
  elements.theme.addEventListener("click", () => {
    const light = document.documentElement.dataset.theme === "light";
    document.documentElement.dataset.theme = light ? "dark" : "light";
    elements.themeLabel.textContent = light ? "浅色预览" : "深色预览";
  });
  elements.export.addEventListener("click", exportSelection);
  elements.copySvg.addEventListener("click", copyCurrentSvg);
  elements.selectDialog.addEventListener("click", () => state.preview && toggleSelection(state.preview));
  elements.dialog.addEventListener("click", event => {
    if (event.target === elements.dialog) elements.dialog.close();
  });
  elements.lockupDialog.addEventListener("click", event => {
    if (event.target === elements.lockupDialog) elements.lockupDialog.close();
  });
  document.addEventListener("keydown", event => {
    if (event.key === "/" && !event.ctrlKey && !event.metaKey && document.activeElement !== elements.search) {
      event.preventDefault();
      elements.search.focus();
    }
  });

  document.querySelector("#conceptCount").textContent = data.brand.length;
  document.querySelector("#iconCount").textContent = data.icons.length;
  renderLockup();
  render();
})();
