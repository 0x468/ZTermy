(() => {
  const p = d => `<path d="${d}"/>`;
  const r = (x, y, width, height, radius = 0) => `<rect x="${x}" y="${y}" width="${width}" height="${height}" rx="${radius}"/>`;
  const c = (x, y, radius, extra = "") => `<circle cx="${x}" cy="${y}" r="${radius}" ${extra}/>`;
  const l = (x1, y1, x2, y2) => `<path d="M${x1} ${y1} ${x2} ${y2}"/>`;
  const icon = (id, name, zh, category, body, tags = []) => ({ id, name, zh, category, body, tags });
  const historyClock = p("M3.4 7.2A7.2 7.2 0 1 1 2.8 12.2M3.4 3.5V7.2H7.1M10 6V10L13 11.8");
  const refreshArrows = p("M16.4 7.2A6.8 6.8 0 0 0 4.2 5.8M16.4 3.5V7.2H12.7M3.6 12.8A6.8 6.8 0 0 0 15.8 14.2M3.6 16.5V12.8H7.3");
  const retryArrow = p("M16.5 7.2A6.8 6.8 0 1 0 16.5 12.8M16.5 3.5V7.2H12.8");

  const brand = [
    icon("brand-ribbon-z", "Ribbon Z", "折带 Z", "brand", `<defs><linearGradient id="brand-ribbon-blue" x1="4" y1="4" x2="15" y2="15" gradientUnits="userSpaceOnUse"><stop stop-color="#7de7ff"/><stop offset="1" stop-color="#3c89ff"/></linearGradient></defs><rect x="1" y="1" width="18" height="18" rx="4.2" fill="#102544" stroke="none"/><path d="M5.55 4.85H15.85L14.45 6.75H9.75L6.75 13.25H15.55L14.15 15.15H4.15L7.15 8.65H11.55L12.95 6.75H4.75Z" fill="url(#brand-ribbon-blue)" stroke="none"/>`, ["z", "monogram", "ribbon", "app-icon"]),
    icon("brand-ribbon-z-prompt", "Ribbon Z · Inset Prompt", "折带 Z · 内嵌提示符", "brand", `<defs><linearGradient id="brand-ribbon-prompt-blue" x1="4" y1="4" x2="15" y2="15" gradientUnits="userSpaceOnUse"><stop stop-color="#7de7ff"/><stop offset="1" stop-color="#3c89ff"/></linearGradient></defs><rect x="1" y="1" width="18" height="18" rx="4.2" fill="#102544" stroke="none"/><path d="M5.55 4.85H15.85L14.45 6.75H9.75L6.75 13.25H15.55L14.15 15.15H4.15L7.15 8.65H11.55L12.95 6.75H4.75Z" fill="url(#brand-ribbon-prompt-blue)" stroke="none"/><path d="M9.2 9.85 10.5 11 9.2 12.15M11.2 12.15H13.2" stroke="#e9f7ff" stroke-width=".64" fill="none" stroke-linecap="round" stroke-linejoin="round"/>`, ["z", "monogram", "terminal", "prompt", "inset", "app-icon"]),
    icon("brand-prompt-tile", "Prompt Tile", "提示符方块", "brand", `<rect x="1" y="1" width="18" height="18" rx="5" fill="#22c55e" stroke="none"/><path d="M5.5 6 9.5 10 5.5 14M11 14H15" stroke="#07130a" stroke-width="1.8" fill="none" stroke-linecap="round" stroke-linejoin="round"/>`, ["terminal", "classic"]),
    icon("brand-z-cut", "Z Cut", "切角 Z", "brand", `<path d="M3 3H17L6 17H17" stroke="#22c55e" stroke-width="3" fill="none" stroke-linecap="round" stroke-linejoin="round"/><path d="M3 12H8" stroke="currentColor" stroke-width="1.7"/>`, ["z", "monogram"]),
    icon("brand-dual-prompt", "Dual Prompt", "双提示符", "brand", `<rect x="1" y="1" width="18" height="18" rx="5" fill="#111c29" stroke="#22c55e"/><path d="M4.5 6 8 9.5 4.5 13M10 6 13.5 9.5 10 13" stroke="#22c55e" stroke-width="1.7" fill="none" stroke-linecap="round" stroke-linejoin="round"/>`, ["ssh", "connection"]),
    icon("brand-node-tunnel", "Node Tunnel", "节点隧道", "brand", `${c(4,10,2,'fill="#22c55e" stroke="none"')}${c(16,5,2,'fill="#22c55e" stroke="none"')}${c(16,15,2,'fill="#22c55e" stroke="none"')}<path d="M6 10H10C13 10 13 5 14 5M10 10C13 10 13 15 14 15" stroke="currentColor" stroke-width="1.5" fill="none"/>`, ["network", "hosts"]),
    icon("brand-window", "Terminal Window", "终端窗口", "brand", `<rect x="1.5" y="2" width="17" height="16" rx="4" fill="#22c55e" stroke="none"/><path d="M1.5 6.5H18.5" stroke="#07130a" stroke-width="1.2"/><circle cx="5" cy="4.3" r=".7" fill="#07130a" stroke="none"/><path d="M5 10 8 12.5 5 15M10 15H14.5" stroke="#07130a" stroke-width="1.5" fill="none" stroke-linecap="round"/>`, ["window", "terminal"]),
    icon("brand-z-prompt", "Z Prompt", "Z 提示符", "brand", `<path d="M3 4H12L5 12H12" stroke="#22c55e" stroke-width="2.6" fill="none" stroke-linecap="round" stroke-linejoin="round"/><path d="M12.5 15H17" stroke="currentColor" stroke-width="1.8"/>`, ["z", "prompt"]),
    icon("brand-pulse", "Pulse Prompt", "脉冲提示符", "brand", `<rect x="1" y="1" width="18" height="18" rx="9" fill="#22c55e" stroke="none"/><path d="M4 10H6.5L8 6 10.5 14 12 10H16" stroke="#07130a" stroke-width="1.6" fill="none" stroke-linecap="round" stroke-linejoin="round"/>`, ["performance", "pulse"]),
    icon("brand-command-cube", "Command Cube", "命令立方体", "brand", `<path d="M10 1.5 18 6V14L10 18.5 2 14V6Z" fill="#22c55e" stroke="none"/><path d="M2 6 10 10.5 18 6M10 10.5V18.5" stroke="#07130a" stroke-width="1.2" fill="none"/><path d="M5.2 7.5 8 9.1 5.2 10.7" stroke="#07130a" stroke-width="1.4" fill="none"/>`, ["package", "command"]),
    icon("brand-split-shell", "Split Shell", "分屏终端", "brand", `<rect x="1.5" y="2" width="17" height="16" rx="4" fill="none" stroke="#22c55e" stroke-width="1.7"/><path d="M10 2V18M4.5 7 7 10 4.5 13M12.5 13H16" stroke="currentColor" stroke-width="1.4" fill="none" stroke-linecap="round"/>`, ["split", "terminal"]),
    icon("brand-orbit", "Orbit Terminal", "轨道终端", "brand", `${c(10,10,4,'fill="#22c55e" stroke="none"')}<ellipse cx="10" cy="10" rx="9" ry="4.3" transform="rotate(-28 10 10)"/><path d="M8 8.3 10 10 8 11.7M11.2 11.8H13" stroke="#07130a" stroke-width="1" fill="none"/>`, ["network", "global"])
  ];

  const icons = [
    icon("hosts", "Hosts", "主机", "chrome", r(3,4,14,10,1.5)+l(7,17,13,17)+l(10,14,10,17), ["monitor","workspace"]),
    icon("terminal-local", "Local terminal", "本地终端", "chrome", r(2.5,3,15,14,2)+p("M5.5 7 8.5 10 5.5 13M10.5 13H14.5"), ["shell","powershell"]),
    icon("terminal-ssh", "SSH terminal", "SSH 终端", "chrome", r(2,4,12,13,2)+p("M5 8 7.5 10.5 5 13M9 13H11.5")+p("M13.5 7.5 16 5 18 7M16 5V12"), ["remote","connection"]),
    icon("settings", "Settings", "设置", "chrome", c(10,10,3)+p("M10 2V4M10 16V18M2 10H4M16 10H18M4.35 4.35 5.8 5.8M14.2 14.2 15.65 15.65M15.65 4.35 14.2 5.8M5.8 14.2 4.35 15.65"), ["gear","preferences"]),
    icon("new-tab", "New tab", "新建标签", "chrome", p("M10 4V16M4 10H16"), ["plus","add"]),
    icon("tab-close", "Close tab", "关闭标签", "chrome", p("M5 5 15 15M15 5 5 15"), ["close","remove"]),
    icon("command-palette", "Command palette", "命令面板", "chrome", r(2,3,16,14,3)+p("M5 7 7.5 10 5 13M9.5 13H14.5"), ["search","commands"]),
    icon("window-controls", "Window controls", "窗口控制", "chrome", p("M3 14H8M11 4H17V10H11ZM12 8H9V14H15V11M3 4 8 9M8 4 3 9"), ["minimize","maximize","restore","close"]),

    icon("local-machine", "Local machine", "本机", "hosts", r(2,3,16,11,2)+l(6,17,14,17)+l(10,14,10,17), ["computer","windows"]),
    icon("server", "Server", "服务器", "hosts", r(3,2.5,14,6,1.5)+r(3,11.5,14,6,1.5)+c(6,5.5,0.8,'fill="currentColor"')+c(6,14.5,0.8,'fill="currentColor"')+l(9,5.5,14,5.5)+l(9,14.5,14,14.5), ["host","rack"]),
    icon("add-host", "Add host", "添加主机", "hosts", r(2,4,11,9,1.5)+l(5,16,10,16)+l(7.5,13,7.5,16)+p("M16 10V18M12 14H20"), ["new","server"]),
    icon("connect", "Connect", "连接", "hosts", p("M3 10H14M10 6 14 10 10 14M16 5V15"), ["open","arrow"]),
    icon("duplicate", "Duplicate profile", "复制配置", "hosts", r(6,6,11,11,2)+p("M3 14V3H14"), ["copy","profile"]),
    icon("recent", "Recent connections", "最近连接", "hosts", historyClock, ["history","clock"]),
    icon("group", "Host group", "主机分组", "hosts", p("M2 6H8L10 8H18V16H2Z"), ["folder","organization"]),
    icon("password", "Password", "密码", "hosts", c(7,9,4)+p("M11 9H18M15 9V12M17 9V11"), ["credentials","authentication"]),
    icon("key", "Private key", "私钥", "hosts", c(6,10,3)+p("M9 10H18M14 10V13M17 10V12"), ["ssh","authentication"]),
    icon("shield-check", "Trusted host", "可信主机", "hosts", p("M10 2 17 5V10C17 14 14 17 10 18 6 17 3 14 3 10V5ZM6.5 10 9 12.5 14 7.5"), ["security","verified"]),

    icon("copy-address", "Copy address", "复制地址", "terminal", r(6,6,11,11,1.5)+p("M3 14V3H14"), ["clipboard","host"]),
    icon("sftp", "Open SFTP", "打开 SFTP", "terminal", p("M2 6H8L10 8H18V16H2Z")+p("M14 4V11M11.5 8.5 14 11 16.5 8.5"), ["files","remote"]),
    icon("composer", "Command composer", "命令撰写栏", "terminal", r(2,3,16,14,2)+p("M5 7H15M5 10H13M5 13H10"), ["write","send"]),
    icon("terminal-search", "Find in terminal", "搜索终端", "terminal", c(8.5,8.5,5.5)+p("M12.5 12.5 17 17")+p("M6 8.5H11"), ["find","history"]),
    icon("session-log", "Session log", "会话日志", "terminal", r(3,2,14,16,2)+p("M6 6H14M6 10H14M6 14H11")+c(15,15,2,'fill="#fb7185" stroke="none"'), ["record","save"]),
    icon("scripts", "Scripts", "脚本", "terminal", p("M5 2H13L17 6V18H5Z M13 2V6H17")+p("M8 10 10 12 8 14M12 14H14"), ["snippet","code"]),
    icon("terminal-history", "Command history", "命令历史", "terminal", historyClock, ["shell","recent"]),
    icon("panel-left", "Move panel left", "面板移至左侧", "terminal", r(2,3,16,14,2)+l(7,3,7,17)+p("M12 7 9 10 12 13"), ["sidebar","layout"]),
    icon("panel-right", "Move panel right", "面板移至右侧", "terminal", r(2,3,16,14,2)+l(13,3,13,17)+p("M8 7 11 10 8 13"), ["sidebar","layout"]),
    icon("run", "Run command", "运行命令", "terminal", p("M6 3 16 10 6 17Z"), ["play","execute"]),
    icon("insert", "Insert command", "插入命令", "terminal", r(2,3,16,14,2)+p("M6 7 9 10 6 13M11 13H15")+l(15,5,15,9)+l(13,7,17,7), ["compose","snippet"]),
    icon("import", "Import library", "导入脚本库", "terminal", p("M3 12V17H17V12M10 3V13M6 9 10 13 14 9"), ["library","download"]),
    icon("export", "Export library", "导出脚本库", "terminal", p("M3 12V17H17V12M10 13V3M6 7 10 3 14 7"), ["library","upload"]),

    icon("home", "Home directory", "主目录", "sftp", p("M2 9 10 2.5 18 9M4 8V18H16V8M8 18V12H12V18"), ["path","user"]),
    icon("parent", "Parent directory", "上级目录", "sftp", p("M5 12 10 7 15 12M10 7V18")+l(3,3,17,3), ["up","path"]),
    icon("refresh", "Refresh", "刷新", "sftp", refreshArrows, ["reload","sync"]),
    icon("upload", "Upload", "上传", "sftp", p("M10 14V3M6 7 10 3 14 7M3 13V17H17V13"), ["transfer","remote"]),
    icon("download", "Download", "下载", "sftp", p("M10 3V14M6 10 10 14 14 10M3 13V17H17V13"), ["transfer","local"]),
    icon("new-folder", "New folder", "新建文件夹", "sftp", p("M2 6H8L10 8H18V17H2Z")+p("M14 2V8M11 5H17"), ["directory","add"]),
    icon("new-file", "New file", "新建文件", "sftp", p("M5 2H12L16 6V18H5ZM12 2V6H16")+p("M10 9V15M7 12H13"), ["document","add"]),
    icon("file", "File", "文件", "sftp", p("M5 2H12L16 6V18H5ZM12 2V6H16"), ["document"]),
    icon("folder", "Folder", "文件夹", "sftp", p("M2 6H8L10 8H18V17H2Z"), ["directory"]),
    icon("rename", "Rename", "重命名", "sftp", p("M4 15 5 11 13.5 2.5 17.5 6.5 9 15ZM12 4 16 8")+p("M3 18H10"), ["edit","name"]),
    icon("bookmark", "Bookmark path", "收藏路径", "sftp", p("M5 2H15V18L10 14 5 18Z"), ["favorite","path"]),
    icon("bookmark-add", "Add bookmark", "添加收藏", "sftp", p("M4 2H13V18L8.5 14 4 18Z")+p("M16 4V10M13 7H19"), ["favorite","plus"]),
    icon("locate-terminal", "Locate terminal directory", "定位终端目录", "sftp", c(10,10,6)+c(10,10,2)+p("M10 1V4M10 16V19M1 10H4M16 10H19"), ["cwd","target"]),
    icon("follow-terminal", "Follow terminal", "跟随终端目录", "sftp", p("M4 5H9L11 7H16V15H4Z")+p("M7 10H13M10 7 13 10 10 13"), ["link","sync"]),
    icon("tree-view", "Tree view", "树形视图", "sftp", c(4,4,1,'fill="currentColor"')+c(9,10,1,'fill="currentColor"')+c(15,15,1,'fill="currentColor"')+c(9,15,1,'fill="currentColor"')+p("M4 5V15H8M4 10H8M9 11V14M10 15H14"), ["hierarchy","folders"]),
    icon("list-view", "List view", "列表视图", "sftp", c(4,5,1,'fill="currentColor"')+c(4,10,1,'fill="currentColor"')+c(4,15,1,'fill="currentColor"')+p("M8 5H17M8 10H17M8 15H17"), ["rows","files"]),
    icon("filter", "Filter files", "筛选文件", "sftp", p("M2 4H18L12 10V16L8 18V10Z"), ["search","funnel"]),
    icon("hidden-visible", "Show hidden files", "显示隐藏文件", "sftp", p("M2 10C4.5 6 7 4.5 10 4.5S15.5 6 18 10C15.5 14 13 15.5 10 15.5S4.5 14 2 10Z")+c(10,10,2.5), ["eye","visibility"]),
    icon("hidden-invisible", "Hide hidden files", "隐藏隐藏文件", "sftp", p("M3 3 17 17M6 5C7.2 4.6 8.5 4.5 10 4.5 13 4.5 15.5 6 18 10 17.2 11.3 16.3 12.3 15.3 13.1M12.8 15C11.9 15.3 11 15.5 10 15.5 7 15.5 4.5 14 2 10 2.7 8.8 3.6 7.7 4.5 6.9"), ["eye-off","visibility"]),
    icon("columns", "Choose columns", "选择列", "sftp", r(2,3,16,14,1.5)+l(8,3,8,17)+l(13,3,13,17), ["table","layout"]),
    icon("open-explorer", "Open in Explorer", "在资源管理器中打开", "sftp", p("M2 6H8L10 8H18V17H2Z")+p("M12 4H18V10M18 4 11 11"), ["external","folder"]),

    icon("transfer-center", "Transfer center", "传输中心", "transfer", p("M3 6H16M12 2 16 6 12 10M17 14H4M8 10 4 14 8 18"), ["upload","download"]),
    icon("queued", "Queued", "等待中", "transfer", c(10,10,7)+p("M10 6V10L13 12"), ["pending","clock"]),
    icon("pause", "Pause", "暂停", "transfer", r(5,4,3,12,1)+r(12,4,3,12,1), ["hold","transfer"]),
    icon("resume", "Resume", "继续", "transfer", p("M6 3 16 10 6 17Z"), ["play","transfer"]),
    icon("cancel", "Cancel", "取消", "transfer", c(10,10,7)+p("M7 7 13 13M13 7 7 13"), ["stop","close"]),
    icon("retry", "Retry", "重试", "transfer", retryArrow, ["refresh","failed"]),
    icon("completed", "Completed", "已完成", "transfer", c(10,10,7)+p("M6 10 9 13 15 7"), ["success","check"]),
    icon("failed", "Failed", "失败", "transfer", c(10,10,7)+p("M7 7 13 13M13 7 7 13"), ["error","danger"]),
    icon("clear-completed", "Clear completed", "清除已完成", "transfer", p("M3 5H17M7 2H13M5 5 6 18H14L15 5M8 9V14M12 9V14")+p("M15 14 17 16 20 12"), ["trash","history"]),
    icon("conflict", "Conflict", "冲突", "transfer", p("M10 2 18 17H2ZM10 7V11")+c(10,14,0.8,'fill="currentColor"'), ["warning","overwrite"]),

    icon("application", "Application", "应用", "settings", c(10,10,8)+p("M10 9V15")+c(10,6,0.8,'fill="currentColor"'), ["about","info"]),
    icon("appearance", "Appearance", "外观", "settings", c(10,10,4)+p("M10 1V4M10 16V19M1 10H4M16 10H19M3.6 3.6 5.7 5.7M14.3 14.3 16.4 16.4M16.4 3.6 14.3 5.7M5.7 14.3 3.6 16.4"), ["theme","sun"]),
    icon("keyboard-shortcuts", "Keyboard shortcuts", "快捷键", "settings", r(2,4,16,12,2)+p("M5 8H7M9 8H11M13 8H15M5 12H7M9 12H15"), ["keys","commands"]),
    icon("security", "Security", "安全", "settings", p("M10 2 17 5V10C17 14 14 17 10 18 6 17 3 14 3 10V5Z")+r(7,9,6,5,1)+p("M8 9V7A2 2 0 0 1 12 7V9"), ["shield","vault"]),
    icon("language", "Language", "语言", "settings", c(10,10,8)+p("M2.5 10H17.5M10 2C13 5 14 8 14 10S13 15 10 18C7 15 6 12 6 10S7 5 10 2"), ["globe","locale"]),
    icon("font", "Font", "字体", "settings", p("M4 17 9 3H12L17 17M6 12H15")+p("M3 3H18"), ["typography","text"]),
    icon("cursor", "Cursor", "光标", "settings", p("M4 2 15 11 10 12 13 18 10.5 19 7.5 13 4 16Z"), ["pointer","terminal"]),
    icon("theme-system", "System theme", "系统主题", "settings", r(2,3,16,12,2)+l(7,18,13,18)+l(10,15,10,18)+p("M10 5A4 4 0 0 0 10 13Z"), ["windows","auto"]),
    icon("theme-dark", "Dark theme", "深色主题", "settings", p("M15.5 13.5A7 7 0 0 1 6.5 4.5 7 7 0 1 0 15.5 13.5Z"), ["moon","night"]),
    icon("accent-color", "Accent color", "强调色", "settings", p("M10 2C5 2 2 5.5 2 10S5.5 18 10 18H12C14 18 14 15 12 14 11 13.5 11.5 12 13 12H15C17 12 18 10.5 18 9 18 5 14.5 2 10 2Z")+c(6,8,1,'fill="currentColor"')+c(9,5.5,1,'fill="currentColor"')+c(13,6.5,1,'fill="currentColor"'), ["palette","color"]),
    icon("vault-portable", "Portable vault", "便携保险库", "settings", r(2.5,6,15,11,2)+p("M7 6V4.8A1.8 1.8 0 0 1 8.8 3H11.2A1.8 1.8 0 0 1 13 4.8V6M2.5 10H17.5")+c(10,12,0.9,'fill="currentColor" stroke="none"')+p("M10 12.9V14.2"), ["password","storage","briefcase","encrypted"]),
    icon("reset", "Reset", "重置", "settings", retryArrow, ["defaults","refresh"]),

    icon("status-info", "Information", "信息", "status", c(10,10,8)+p("M10 9V15")+c(10,6,0.8,'fill="currentColor"'), ["message","help"]),
    icon("status-success", "Success", "成功", "status", c(10,10,8)+p("M6 10 9 13 15 7"), ["check","complete"]),
    icon("status-warning", "Warning", "警告", "status", p("M10 2 18 17H2ZM10 7V11")+c(10,14,0.8,'fill="currentColor"'), ["alert","attention"]),
    icon("status-error", "Error", "错误", "status", c(10,10,8)+p("M7 7 13 13M13 7 7 13"), ["danger","failed"]),
    icon("status-loading", "Loading", "加载中", "status", p("M17 10A7 7 0 1 1 10 3"), ["spinner","progress"]),
    icon("status-connected", "Connected", "已连接", "status", c(10,10,6,'fill="#22c55e" stroke="none"')+p("M7 10 9 12 13 8"), ["online","ssh"]),
    icon("status-disconnected", "Disconnected", "已断开", "status", c(10,10,7)+p("M6 10H14"), ["offline","ssh"]),
    icon("status-recording", "Recording", "正在记录", "status", c(10,10,6,'fill="#fb7185" stroke="none"'), ["log","active"])
  ];

  window.ZtermyIconData = {
    approvedBrandId: "brand-ribbon-z-prompt",
    categories: [
      { id: "all", label: "全部" },
      { id: "chrome", label: "标题栏" },
      { id: "hosts", label: "Hosts" },
      { id: "terminal", label: "终端" },
      { id: "sftp", label: "SFTP" },
      { id: "transfer", label: "传输" },
      { id: "settings", label: "设置" },
      { id: "status", label: "状态" }
    ],
    categoryLabels: { brand: "品牌", chrome: "标题栏", hosts: "Hosts", terminal: "终端", sftp: "SFTP", transfer: "传输", settings: "设置", status: "状态" },
    brand,
    icons
  };
})();
