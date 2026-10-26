#!/usr/bin/env python3
"""Mantis Sprite Studio: the art pipeline for MantisPlanet's creatures and planet colours.

    python mantis_sprite_studio.py            (Windows: Python 3.10+, with  pip install pillow)

Pick a creature, read its spec, copy or save a template, draw over it in Krita or Photoshop at that scale,
import it: the studio shrinks it to game pixels, snaps it to the creature palette, keeps its anchors, and
previews it in its biome (from bundled plates, or live through the game's own renderer). Export writes the
pack to the SD card's /mp-sprites folder, which the game reads at boot.
"""
import os, sys, json, shutil, subprocess, tempfile, threading, copy, time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, colorchooser
from PIL import Image, ImageTk, ImageDraw
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import spritelib as L

APP = 'Mantis Sprite Studio'
PXR = 160 / 1.10
CONF = os.path.join(os.environ.get('APPDATA', os.path.expanduser('~')), 'MantisSpriteStudio', 'settings.json')
GROUPS = [('Deer and kin', lambda c: c['kind'] == 0), ('Small mammals', lambda c: c['kind'] in (1, 5, 6)),
          ('Birds', lambda c: c['kind'] in (2, 4)), ('Insects', lambda c: c['kind'] == 3), ('In the water', lambda c: c['kind'] in (7, 8))]
DEER_POSES = ['stand', 'walk', 'bound', 'graze', 'alert', 'stomp', 'sniff', 'nurse', 'away']
TAILS = ['down', 'flag', 'flick']

def load_settings():
    try:
        with open(CONF) as f: return json.load(f)
    except Exception:
        return {}

def save_settings(s):
    os.makedirs(os.path.dirname(CONF), exist_ok=True)
    with open(CONF, 'w') as f: json.dump(s, f, indent=1)

class Studio(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP); self.geometry('1380x860'); self.minsize(1100, 720)
        self.settings = load_settings()
        self.best = L.load_bestiary(); self.pal = L.Palette(); self.dfl = L.Defaults()
        with open(os.path.join(L.DEFAULTS, 'plates.json')) as f: self.plates = json.load(f)
        self.project = L.Project(self.settings.get('project') or os.path.join(os.path.expanduser('~'), 'MantisPlanet Art'))
        self.creature = None; self.asset = None; self.tier = 'mid'
        self.anim_job = None; self.anim_frames = []; self.anim_i = 0; self.live_busy = False
        self._style(); self._menu(); self._layout()
        self.planet = L.load_planet(self.project.planet_path()) if os.path.exists(self.project.planet_path()) else L.load_planet()
        self._fill_tree(); self._fill_planet()
        first = self.tree.get_children(self.tree.get_children()[0])[0]
        self.tree.selection_set(first); self.tree.see(first)
        self.status('Project: %s' % self.project.folder)
        self.protocol('WM_DELETE_WINDOW', self._quit)

    # ------------------------------------------------------------------ look and layout
    def _style(self):
        st = ttk.Style(self)
        if 'clam' in st.theme_names(): st.theme_use('clam')
        bg, panel, fg, acc = '#23262b', '#2c3036', '#e6e6e6', '#d8a740'
        self.configure(bg=bg)
        st.configure('.', background=panel, foreground=fg, fieldbackground='#1d2024', bordercolor='#3a3f46', font=('Segoe UI', 10))
        st.configure('TFrame', background=panel); st.configure('TLabel', background=panel, foreground=fg)
        st.configure('Head.TLabel', font=('Segoe UI Semibold', 12), foreground=acc)
        st.configure('Dim.TLabel', foreground='#9aa0a8')
        st.configure('TButton', padding=(10, 4)); st.map('TButton', background=[('active', '#3a4048')])
        st.configure('Accent.TButton', foreground='#1a1a1a', background=acc); st.map('Accent.TButton', background=[('active', '#e8bb58')])
        st.configure('Treeview', background='#1d2024', fieldbackground='#1d2024', foreground=fg, rowheight=24, borderwidth=0)
        st.map('Treeview', background=[('selected', '#41505f')])
        st.configure('TNotebook', background=bg, borderwidth=0); st.configure('TNotebook.Tab', padding=(16, 6))
        st.map('TNotebook.Tab', background=[('selected', panel)], foreground=[('selected', acc)])
        st.configure('TCheckbutton', background=panel, foreground=fg); st.configure('TRadiobutton', background=panel, foreground=fg)
        self.CANVAS_BG = '#15171a'

    def _menu(self):
        m = tk.Menu(self)
        f = tk.Menu(m, tearoff=0)
        f.add_command(label='Open project folder...', command=self.open_project)
        f.add_command(label='Export pack to SD card...', command=self.export_pack, accelerator='Ctrl+E')
        f.add_separator(); f.add_command(label='Settings...', command=self.settings_dialog)
        f.add_separator(); f.add_command(label='Quit', command=self._quit)
        m.add_cascade(label='File', menu=f)
        t = tk.Menu(m, tearoff=0)
        t.add_command(label='Export creature palette (.gpl, .aco, .png)...', command=self.export_palette)
        t.add_command(label='Export all specs...', command=self.export_specs)
        t.add_command(label='Build the live renderer (how to)...', command=self.live_help)
        m.add_cascade(label='Tools', menu=t)
        h = tk.Menu(m, tearoff=0); h.add_command(label='About', command=lambda: messagebox.showinfo(APP, __doc__)); m.add_cascade(label='Help', menu=h)
        self.config(menu=m); self.bind('<Control-e>', lambda e: self.export_pack())

    def _layout(self):
        outer = ttk.Frame(self); outer.pack(fill='both', expand=True)
        pw = ttk.PanedWindow(outer, orient='horizontal'); pw.pack(fill='both', expand=True, padx=6, pady=(6, 0))
        left = ttk.Frame(pw); pw.add(left, weight=0)
        ttk.Label(left, text='Bestiary', style='Head.TLabel').pack(anchor='w', padx=8, pady=(8, 4))
        self.tree = ttk.Treeview(left, show='tree', selectmode='browse', height=30); self.tree.pack(fill='both', expand=True, padx=6, pady=(0, 6))
        self.tree.bind('<<TreeviewSelect>>', self.on_creature)
        ttk.Button(left, text='Export pack to SD...', style='Accent.TButton', command=self.export_pack).pack(fill='x', padx=6, pady=(0, 8))
        right = ttk.Frame(pw); pw.add(right, weight=1)
        self.nb = ttk.Notebook(right); self.nb.pack(fill='both', expand=True)
        self._assets_tab(); self._preview_tab(); self._planet_tab(); self._spec_tab()
        self.statusvar = tk.StringVar()
        ttk.Label(outer, textvariable=self.statusvar, style='Dim.TLabel', anchor='w').pack(fill='x', padx=10, pady=4)

    def status(self, s): self.statusvar.set(s)

    # ------------------------------------------------------------------ the bestiary
    def _fill_tree(self):
        self.tree.delete(*self.tree.get_children())
        for g, f in GROUPS:
            gid = self.tree.insert('', 'end', text=g, open=True)
            for c in self.best['creatures']:
                if f(c): self.tree.insert(gid, 'end', iid=c['id'], text='  ' + c['name'] + ('  (recolour)' if c.get('recolour_of') else ''))

    def on_creature(self, _e=None):
        sel = self.tree.selection()
        if not sel: return
        c = next((x for x in self.best['creatures'] if x['id'] == sel[0]), None)
        if not c: return
        self.creature = c
        self.spec.configure(state='normal'); self.spec.delete('1.0', 'end'); self.spec.insert('1.0', L.spec_text(c, self.best)); self.spec.configure(state='disabled')
        self._fill_assets(); self._preview_controls(); self.refresh_preview()

    # ------------------------------------------------------------------ assets
    def _assets_tab(self):
        tab = ttk.Frame(self.nb); self.nb.add(tab, text='  Assets  ')
        top = ttk.Frame(tab); top.pack(fill='both', expand=True, padx=8, pady=8)
        cols = ('size', 'frames', 'status')
        self.alist = ttk.Treeview(top, columns=cols, show='tree headings', height=12, selectmode='browse')
        self.alist.heading('#0', text='Asset'); self.alist.column('#0', width=260)
        for c, w in zip(cols, (90, 60, 160)): self.alist.heading(c, text=c.capitalize()); self.alist.column(c, width=w, anchor='center')
        self.alist.pack(side='left', fill='y'); self.alist.bind('<<TreeviewSelect>>', self.on_asset)
        det = ttk.Frame(top); det.pack(side='left', fill='both', expand=True, padx=(10, 0))
        self.atitle = ttk.Label(det, text='', style='Head.TLabel'); self.atitle.pack(anchor='w')
        self.anote = ttk.Label(det, text='', style='Dim.TLabel', wraplength=640, justify='left'); self.anote.pack(anchor='w', pady=(2, 6))
        bar = ttk.Frame(det); bar.pack(anchor='w', pady=(0, 6))
        for txt, cmd in (('Copy template', self.copy_template), ('Save template...', self.save_template), ('Import art...', self.import_art)):
            ttk.Button(bar, text=txt, command=cmd).pack(side='left', padx=(0, 6))
        ttk.Button(bar, text='Undo import', command=self.undo_import).pack(side='left', padx=(0, 6))
        ttk.Button(bar, text='Revert to game art', command=self.revert_asset).pack(side='left')
        ab = ttk.Frame(det); ab.pack(anchor='w', pady=(0, 4))
        ttk.Label(ab, text='Anchor editor:').pack(side='left')
        self.anchor_mode = tk.StringVar(value='')
        for txt, v in (('off', ''), ('head / attach', 'head'), ('tail root', 'tail')):
            ttk.Radiobutton(ab, text=txt, value=v, variable=self.anchor_mode, command=self.draw_asset).pack(side='left', padx=4)
        ttk.Label(ab, text='  frame').pack(side='left')
        self.aframe = tk.IntVar(value=0); self.aframe_spin = ttk.Spinbox(ab, from_=1, to=4, width=3, command=self._frame_changed); self.aframe_spin.pack(side='left', padx=4)
        self.aframe_spin.set(1)
        self.acanvas = tk.Canvas(det, bg=self.CANVAS_BG, highlightthickness=0, height=330); self.acanvas.pack(fill='both', expand=True)
        self.acanvas.bind('<Button-1>', self.on_anchor_click)
        ttk.Label(tab, text='Messages', style='Dim.TLabel').pack(anchor='w', padx=8)
        self.msgs = tk.Text(tab, height=6, bg='#1d2024', fg='#d9c27a', relief='flat', font=('Consolas', 9)); self.msgs.pack(fill='x', padx=8, pady=(0, 8))

    def msg(self, text):
        self.msgs.insert('end', time.strftime('%H:%M:%S  ') + text + '\n'); self.msgs.see('end')

    def _fill_assets(self):
        self.alist.delete(*self.alist.get_children())
        self.assets = L.assets_for(self.creature)
        if not self.assets:
            self.atitle.configure(text=self.creature['name']); self.anote.configure(text=L.spec_text(self.creature, self.best).split('\n\n')[-1])
            self.acanvas.delete('all'); return
        for i, a in enumerate(self.assets):
            pid = self.alist.insert('', 'end', iid='a%d' % i, text=a.label, values=('', a.frames, ''), open=False)
            for t in L.TIERS:
                if t in a.sizes:
                    w, h = a.sizes[t]
                    self.alist.insert(pid, 'end', iid='a%d:%s' % (i, t), text='   ' + t, values=('%dx%d' % (w, h), a.frames, self._state(a, t)))
        first = 'a0:mid' if self.alist.exists('a0:mid') else self.alist.get_children('a0')[0]
        self.alist.item('a0', open=True); self.alist.selection_set(first)

    def _state(self, a, t):
        if self.project.has(a, t): return 'yours'
        return 'game art' if self.dfl.sprite(a, t) else 'empty'

    def on_asset(self, _e=None):
        sel = self.alist.selection()
        if not sel: return
        k = sel[0]
        if ':' not in k:
            kids = self.alist.get_children(k)
            if kids: self.alist.item(k, open=True); self.alist.selection_set(kids[min(1, len(kids) - 1)])
            return
        i, t = k[1:].split(':')
        self.asset, self.tier = self.assets[int(i)], t
        w, h = self.asset.sizes[t]
        self.atitle.configure(text='%s  -  %s  (%dx%d, %d frame%s)' % (self.creature['name'], self.asset.label, w, h, self.asset.frames, '' if self.asset.frames == 1 else 's'))
        self.anote.configure(text=self.asset.note + ('\nMarkers: ' + self.asset.markers if self.asset.markers else ''))
        self.aframe_spin.configure(to=self.asset.frames); self.aframe_spin.set(1)
        self.draw_asset()

    def current(self, a=None, t=None):
        a = a or self.asset; t = t or self.tier
        return self.project.load(a, t) or L.default_sprite(a, t, self.dfl)

    def _frame_changed(self): self.draw_asset()

    def draw_asset(self):
        if not self.asset: return
        spr = self.current()
        cw = max(400, self.acanvas.winfo_width()); ch = max(260, self.acanvas.winfo_height())
        n = max(1, min((cw - 20) // max(1, spr.w * spr.frames + spr.frames), (ch - 20) // max(1, spr.h)))
        self.acanvas.delete('all')
        img = Image.new('RGBA', (spr.w * spr.frames * n + (spr.frames - 1) * 8, spr.h * n), (0, 0, 0, 0))
        for f in range(spr.frames): img.paste(spr.to_image(self.pal, f, n), (f * (spr.w * n + 8), 0))
        chk = Image.new('RGBA', img.size, (34, 37, 41, 255)); d = ImageDraw.Draw(chk)
        for y in range(0, img.height, 8):
            for x in range(0, img.width, 8):
                if (x // 8 + y // 8) % 2: d.rectangle([x, y, x + 7, y + 7], fill=(42, 46, 51, 255))
        chk.alpha_composite(img)
        self._acimg = ImageTk.PhotoImage(chk); self._acn = n; self._acspr = spr
        self._acx, self._acy = 10, 10
        self.acanvas.create_image(10, 10, image=self._acimg, anchor='nw')
        for f in range(spr.frames):
            ox = 10 + f * (spr.w * n + 8)
            self.acanvas.create_rectangle(ox, 10, ox + spr.w * n, 10 + spr.h * n, outline='#3f4650')
            a = spr.anchors[f] if f < len(spr.anchors) and spr.anchors[f] else []
            if len(a) >= 2:
                x, y = ox + a[0] * n + n // 2, 10 + a[1] * n + n // 2
                self.acanvas.create_oval(x - 5, y - 5, x + 5, y + 5, outline='#ff40ff', width=2)
            if len(a) >= 4:
                x, y = ox + a[2] * n + n // 2, 10 + a[3] * n + n // 2
                self.acanvas.create_oval(x - 5, y - 5, x + 5, y + 5, outline='#40ffff', width=2)
        if self.anchor_mode.get():
            self.acanvas.create_text(12, 14 + spr.h * n, anchor='nw', fill='#d8a740',
                                     text='Click to place the %s point on frame %s.' % ('head/attach' if self.anchor_mode.get() == 'head' else 'tail-root', self.aframe_spin.get()))

    def on_anchor_click(self, e):
        mode = self.anchor_mode.get()
        if not mode or not self.asset or not self.asset.markers: return
        if mode == 'tail' and L.anchors_per_frame(self.asset) < 2: return
        spr = self.current(); n = self._acn
        f = int(self.aframe_spin.get()) - 1
        ox = 10 + f * (spr.w * n + 8)
        x, y = (e.x - ox) // n, (e.y - 10) // n
        if not (0 <= x < spr.w and 0 <= y < spr.h): return
        a = list(spr.anchors[f]) if f < len(spr.anchors) and spr.anchors[f] else ([0, 0, 0, 0] if L.anchors_per_frame(self.asset) == 2 else [0, 0])
        if mode == 'head': a[0], a[1] = x, y + (1 if L.anchors_per_frame(self.asset) == 1 else 0)
        else: a[2], a[3] = x, y
        while len(spr.anchors) < spr.frames: spr.anchors.append([])
        spr.anchors[f] = a
        self.project.store(self.asset, self.tier, spr, self.project.meta['sources'].get(self.asset.key(self.tier), '(anchors edited)'))
        self.msg('%s frame %d: %s point set to %d, %d' % (self.asset.key(self.tier), f + 1, mode, x, y))
        self._refresh_state(); self.draw_asset(); self.refresh_preview()

    def _refresh_state(self):
        for i, a in enumerate(self.assets):
            for t in L.TIERS:
                iid = 'a%d:%s' % (i, t)
                if self.alist.exists(iid): self.alist.set(iid, 'status', self._state(a, t))

    def _template(self):
        cur = self.project.load(self.asset, self.tier)
        return L.make_template(self.asset, self.tier, self.dfl, self.pal, current=cur)

    def copy_template(self):
        if not self.asset: return
        img, n = self._template()
        try:
            L.copy_image_to_clipboard(img)
            self.status('Template copied (%dx%d, game pixels shown %dx%d). Paste it into Krita and draw at that scale.' % (img.width, img.height, n, n))
        except Exception as ex:
            messagebox.showwarning(APP, 'Could not use the clipboard here (%s). Use "Save template..." instead.' % ex)

    def save_template(self):
        if not self.asset: return
        img, n = self._template()
        name = '%s__%s__%dx.png' % (self.creature['id'], self.asset.file(self.tier), n)
        p = filedialog.asksaveasfilename(defaultextension='.png', initialfile=name, filetypes=[('PNG', '*.png')])
        if p: img.save(p); self.status('Saved %s (each game pixel is %dx%d).' % (p, n, n))

    def import_art(self):
        if not self.asset: return
        p = filedialog.askopenfilename(filetypes=[('Images', '*.png *.jpg *.jpeg *.webp *.bmp'), ('All', '*.*')])
        if not p: return
        try:
            prev = self.current()
            res = L.import_art(p, self.asset, self.tier, self.pal, prev.anchors)
        except Exception as ex:
            messagebox.showerror(APP, 'Could not import %s:\n%s' % (os.path.basename(p), ex)); return
        mp = self.project.mps_path(self.asset, self.tier)
        if os.path.exists(mp): shutil.copyfile(mp, mp + '.bak')
        self.project.store(self.asset, self.tier, res.sprite, p)
        self.msg('Imported %s -> %s  (scale %dx)' % (os.path.basename(p), self.asset.key(self.tier), res.scale))
        for w in res.warnings: self.msg('   ' + w)
        self._refresh_state(); self.draw_asset(); self.refresh_preview()

    def undo_import(self):
        if not self.asset: return
        mp = self.project.mps_path(self.asset, self.tier)
        if os.path.exists(mp + '.bak'): shutil.move(mp + '.bak', mp); self.msg('Undid the last import of ' + self.asset.key(self.tier))
        else: self.status('Nothing to undo for this asset.')
        self._refresh_state(); self.draw_asset(); self.refresh_preview()

    def revert_asset(self):
        if not self.asset or not self.project.has(self.asset, self.tier): return
        if messagebox.askyesno(APP, 'Remove your art for %s and use the game art?' % self.asset.key(self.tier)):
            self.project.remove(self.asset, self.tier); self.msg('Reverted ' + self.asset.key(self.tier))
            self._refresh_state(); self.draw_asset(); self.refresh_preview()

    # ------------------------------------------------------------------ preview
    def _preview_tab(self):
        tab = ttk.Frame(self.nb); self.nb.add(tab, text='  Preview  ')
        ctl = ttk.Frame(tab); ctl.pack(fill='x', padx=8, pady=8)
        self.pv_biome = tk.StringVar(value='meadow'); self.pv_dist = tk.StringVar(value='all')
        self.pv_sex = tk.StringVar(value='doe'); self.pv_pose = tk.StringVar(value='stand'); self.pv_tail = tk.StringVar(value='down')
        self.pv_moving = tk.BooleanVar(value=False); self.pv_play = tk.BooleanVar(value=True); self.pv_fps = tk.IntVar(value=6)
        self.live = tk.BooleanVar(value=bool(self.settings.get('live', False)))
        self.pv_tod = tk.DoubleVar(value=0.45); self.pv_weather = tk.StringVar(value='clear')
        def lab(t): ttk.Label(ctl, text=t).pack(side='left', padx=(10, 2))
        lab('Biome'); ttk.Combobox(ctl, textvariable=self.pv_biome, values=self.plates['plates'], width=11, state='readonly').pack(side='left')
        lab('Distance'); ttk.Combobox(ctl, textvariable=self.pv_dist, values=['all', 'near', 'mid', 'far'], width=6, state='readonly').pack(side='left')
        self.deer_ctl = ttk.Frame(ctl); self.deer_ctl.pack(side='left')
        ttk.Label(self.deer_ctl, text='Sex').pack(side='left', padx=(10, 2))
        ttk.Combobox(self.deer_ctl, textvariable=self.pv_sex, values=['doe', 'buck', 'fawn', 'cow'], width=6, state='readonly').pack(side='left')
        ttk.Label(self.deer_ctl, text='Pose').pack(side='left', padx=(10, 2))
        ttk.Combobox(self.deer_ctl, textvariable=self.pv_pose, values=DEER_POSES, width=7, state='readonly').pack(side='left')
        ttk.Label(self.deer_ctl, text='Tail').pack(side='left', padx=(10, 2))
        ttk.Combobox(self.deer_ctl, textvariable=self.pv_tail, values=TAILS, width=6, state='readonly').pack(side='left')
        self.other_ctl = ttk.Checkbutton(ctl, text='Moving', variable=self.pv_moving, command=self.refresh_preview)
        ttk.Checkbutton(ctl, text='Play', variable=self.pv_play, command=self._animate).pack(side='left', padx=(14, 2))
        ttk.Spinbox(ctl, from_=1, to=20, width=3, textvariable=self.pv_fps).pack(side='left'); ttk.Label(ctl, text='fps').pack(side='left')
        for v in (self.pv_biome, self.pv_dist, self.pv_sex, self.pv_pose, self.pv_tail): v.trace_add('write', lambda *_: self.refresh_preview())
        live = ttk.Frame(tab); live.pack(fill='x', padx=8)
        ttk.Checkbutton(live, text='Live renderer (the game itself)', variable=self.live, command=self._live_toggled).pack(side='left')
        ttk.Label(live, text='   Time').pack(side='left')
        ttk.Scale(live, from_=0.0, to=1.0, variable=self.pv_tod, length=220, command=lambda _v: None).pack(side='left', padx=4)
        ttk.Label(live, text='Weather').pack(side='left', padx=(10, 2))
        ttk.Combobox(live, textvariable=self.pv_weather, values=['clear', 'cloudy', 'rain', 'storm', 'fog', 'snow'], width=7, state='readonly').pack(side='left')
        ttk.Button(live, text='Render', command=self.refresh_preview).pack(side='left', padx=10)
        self.pv_note = ttk.Label(live, text='', style='Dim.TLabel'); self.pv_note.pack(side='left', padx=8)
        self.pcanvas = tk.Canvas(tab, bg=self.CANVAS_BG, highlightthickness=0); self.pcanvas.pack(fill='both', expand=True, padx=8, pady=8)

    def _preview_controls(self):
        deer = self.creature and (self.creature.get('parts') == 'deer' or self.creature.get('recolour_of') == 'deer')
        if deer: self.other_ctl.pack_forget(); self.deer_ctl.pack(side='left')
        else: self.deer_ctl.pack_forget(); self.other_ctl.pack(side='left', padx=10)

    def _live_toggled(self):
        self.settings['live'] = self.live.get(); save_settings(self.settings); self.refresh_preview()

    def _deer_asset(self, stem):
        deer = next(c for c in self.best['creatures'] if c['id'] == 'deer')
        return next((a for a in L.assets_for(deer) if a.stem == stem), None)

    def _frames_offline(self, tier):
        """This creature's frames at one tier, as game-pixel RGBA images (and its height in metres for placing it)."""
        c = self.creature
        if c.get('parts') == 'deer' or c.get('recolour_of') == 'deer':
            pose = self.pv_pose.get()
            body = self.current(self._deer_asset('body_' + pose), tier)
            sex = self.pv_sex.get(); tail_st = self.pv_tail.get()
            ant = self.current(self._deer_asset('antlers_away' if pose == 'away' else 'antlers'), tier)
            tail = self.current(self._deer_asset(('tail_away_' if pose == 'away' else 'tail_') + tail_st), tier)
            out = []
            for f in range(body.frames):
                px, w, h = L.deer_variant(body, f, sex, ant, tail, tier)
                im = Image.new('RGBA', (w, h), (0, 0, 0, 0)); im.putdata([(self.pal.rgb(v) + (255,)) if v else (0, 0, 0, 0) for v in px])
                out.append(im)
            return out, None
        assets = L.assets_for(c)
        a = assets[1] if self.pv_moving.get() and len(assets) > 1 else assets[0]
        if tier not in a.sizes: return [], None
        spr = self.current(a, tier)
        return [spr.to_image(self.pal, f) for f in range(spr.frames)], c.get('h_m', 0.3)

    def refresh_preview(self):
        if not self.creature: return
        if self.live.get(): return self._render_live()
        self.pv_note.configure(text='Bundled noon plate. Turn on the live renderer for the real game, any time and weather.')
        plate = Image.open(os.path.join(L.DEFAULTS, 'plates', self.pv_biome.get() + '.png')).convert('RGBA')
        tiers = ['far', 'mid', 'near'] if self.pv_dist.get() == 'all' else [self.pv_dist.get()]
        deer = self.creature.get('parts') == 'deer' or self.creature.get('recolour_of') == 'deer'
        sets = []
        for k, t in enumerate(tiers):
            frames, hm = self._frames_offline(t)
            if not frames: continue
            if deer: dist = {'far': 18.0, 'mid': 9.0, 'near': 4.5}[t]
            else: dist = max(1.4, hm * PXR / max(1, frames[0].height))
            x = 160 if len(tiers) == 1 else 60 + k * 100
            sets.append((frames, x, 2 * (self.plates['horizon'] + self.plates['zk'] / dist)))
        n = max([len(s[0]) for s in sets] + [1])
        self.anim_frames = []
        for f in range(n):
            im = plate.copy()
            for frames, x, base in sets:
                fr = frames[f % len(frames)]
                big = fr.resize((fr.width * 2, fr.height * 2), Image.NEAREST)
                im.alpha_composite(big, (int(x - big.width / 2), int(base - big.height)))
            self.anim_frames.append(im)
        self.anim_i = 0; self._animate()

    def _render_live(self):
        exe = self.settings.get('mpview') or os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'host', 'mpview.exe' if os.name == 'nt' else 'mpview')
        if not os.path.exists(exe):
            self.pv_note.configure(text='Live renderer not found: Tools > Build the live renderer, then set its path in Settings.')
            self.live.set(False); return self.refresh_preview()
        if self.live_busy: return
        self.live_busy = True; self.pv_note.configure(text='Rendering in the game...')
        tmp = tempfile.mkdtemp(prefix='mss_')
        card = os.path.join(tmp, 'card'); self.project.export(card)
        deer = self.creature.get('parts') == 'deer' or self.creature.get('recolour_of') == 'deer'
        dist = {'near': 4.5, 'mid': 9.0, 'far': 18.0}.get(self.pv_dist.get(), 6.0)
        pose = self.pv_pose.get() if deer else ('move' if self.pv_moving.get() else 'stand')
        args = [exe, '--sd', card, '--biome', self.pv_biome.get(), '--tod', '%.3f' % self.pv_tod.get(), '--weather', self.pv_weather.get(),
                '--creature', self.creature['id'], '--sex', self.pv_sex.get(), '--pose', pose, '--dist', '%.2f' % dist,
                '--frames', '8', '--fps', str(self.pv_fps.get()), '--out', os.path.join(tmp, 'f')]
        def work():
            try:
                r = subprocess.run(args, capture_output=True, text=True, timeout=60)
                files = [l.strip() for l in r.stdout.splitlines() if l.strip().endswith('.ppm')]
                frames = [Image.open(p).convert('RGBA') for p in files]
                err = None if frames else (r.stderr.strip() or 'no frames came back')
            except Exception as ex:
                frames, err = [], str(ex)
            self.after(0, lambda: self._live_done(frames, err, tmp))
        threading.Thread(target=work, daemon=True).start()

    def _live_done(self, frames, err, tmp):
        self.live_busy = False
        shutil.rmtree(tmp, ignore_errors=True)
        if err: self.pv_note.configure(text='Live renderer: ' + err[:120]); self.msg('Live renderer: ' + err); return
        self.pv_note.configure(text='Rendered by the game (%d frames).' % len(frames))
        self.anim_frames = frames; self.anim_i = 0; self._animate()

    def _animate(self):
        if self.anim_job: self.after_cancel(self.anim_job); self.anim_job = None
        if not self.anim_frames: return
        cw = max(320, self.pcanvas.winfo_width()); ch = max(240, self.pcanvas.winfo_height())
        z = max(1, min(cw // 320, ch // 240))
        im = self.anim_frames[self.anim_i % len(self.anim_frames)].resize((320 * z, 240 * z), Image.NEAREST)
        self._pvimg = ImageTk.PhotoImage(im)
        self.pcanvas.delete('all'); self.pcanvas.create_image(cw // 2, ch // 2, image=self._pvimg)
        if self.pv_play.get() and len(self.anim_frames) > 1:
            self.anim_i += 1
            self.anim_job = self.after(int(1000 / max(1, self.pv_fps.get())), self._animate)

    # ------------------------------------------------------------------ planet colours
    def _planet_tab(self):
        tab = ttk.Frame(self.nb); self.nb.add(tab, text='  Planet colours  ')
        bar = ttk.Frame(tab); bar.pack(fill='x', padx=8, pady=8)
        self.planet_on = tk.BooleanVar(value=bool(self.project.meta.get('planet_enabled')))
        ttk.Checkbutton(bar, text='Include this planet in the pack', variable=self.planet_on, command=self._planet_toggle).pack(side='left')
        for txt, cmd in (('Load planet...', self.load_planet), ('Save', self.save_planet), ('Reset to Earth', self.reset_planet),
                         ('Export swatch...', self.export_swatch), ('Import swatch...', self.import_swatch), ('Preview live', self.preview_planet)):
            ttk.Button(bar, text=txt, command=cmd).pack(side='left', padx=(8, 0))
        self.pname = tk.StringVar(); ttk.Label(bar, text='   Name').pack(side='left'); ttk.Entry(bar, textvariable=self.pname, width=18).pack(side='left', padx=4)
        body = ttk.Frame(tab); body.pack(fill='both', expand=True, padx=8, pady=(0, 8))
        self.ptree = ttk.Treeview(body, columns=('rgb',), show='tree headings', selectmode='browse')
        self.ptree.heading('#0', text='Colour'); self.ptree.column('#0', width=300); self.ptree.heading('rgb', text='RGB'); self.ptree.column('rgb', width=120, anchor='center')
        sb = ttk.Scrollbar(body, orient='vertical', command=self.ptree.yview); self.ptree.configure(yscrollcommand=sb.set)
        self.ptree.pack(side='left', fill='y'); sb.pack(side='left', fill='y')
        self.ptree.bind('<Double-1>', self.edit_colour); self.ptree.bind('<Return>', self.edit_colour)
        right = ttk.Frame(body); right.pack(side='left', fill='both', expand=True, padx=(10, 0))
        ttk.Label(right, text='Double-click a colour to change it. Everything the game lights, fogs and reflects follows from these.', style='Dim.TLabel', wraplength=600).pack(anchor='w')
        self.pl_canvas = tk.Canvas(right, bg=self.CANVAS_BG, highlightthickness=0); self.pl_canvas.pack(fill='both', expand=True, pady=6)
        self._swatch_icons = {}

    def _swatch_icon(self, rgb):
        key = tuple(rgb)
        if key not in self._swatch_icons:
            im = Image.new('RGB', (28, 14), key); self._swatch_icons[key] = ImageTk.PhotoImage(im)
        return self._swatch_icons[key]

    def _fill_planet(self):
        self.pname.set(self.planet.get('name', 'Earth'))
        self.ptree.delete(*self.ptree.get_children()); self.slot_paths = {}
        groups = {}
        for g, name, path in L.planet_slots(self.planet):
            if g not in groups: groups[g] = self.ptree.insert('', 'end', text=g, open=False)
            rgb = L.planet_get(self.planet, path)
            iid = self.ptree.insert(groups[g], 'end', text='  ' + name, image=self._swatch_icon(rgb), values=('%d, %d, %d' % rgb,))
            self.slot_paths[iid] = path
        self._draw_planet_swatch()

    def _draw_planet_swatch(self):
        im, _ = L.planet_swatch(self.planet, cell=14, cols=40)
        self._plimg = ImageTk.PhotoImage(im); self.pl_canvas.delete('all'); self.pl_canvas.create_image(4, 4, image=self._plimg, anchor='nw')

    def edit_colour(self, _e=None):
        sel = self.ptree.selection()
        if not sel or sel[0] not in self.slot_paths: return
        path = self.slot_paths[sel[0]]
        rgb = L.planet_get(self.planet, path)
        c = colorchooser.askcolor(color='#%02x%02x%02x' % rgb, title=self.ptree.item(sel[0], 'text').strip())
        if not c or not c[0]: return
        new = tuple(int(v) for v in c[0])
        L.planet_set(self.planet, path, new)
        self.ptree.item(sel[0], image=self._swatch_icon(new), values=('%d, %d, %d' % new,))
        self._draw_planet_swatch(); self.status('Changed %s. Save to keep it.' % self.ptree.item(sel[0], 'text').strip())

    def _planet_toggle(self):
        self.project.meta['planet_enabled'] = self.planet_on.get(); self.project.save()

    def save_planet(self):
        self.planet['name'] = self.pname.get() or 'Unnamed planet'
        with open(self.project.planet_path(), 'w') as f: json.dump(self.planet, f, indent=1)
        self.status('Planet saved to the project. It goes to the card with "Export pack" when included.')

    def load_planet(self):
        p = filedialog.askopenfilename(filetypes=[('Planet', '*.json')])
        if not p: return
        try:
            pl = L.load_planet(p); L.planet_slots(pl)
        except Exception as ex:
            messagebox.showerror(APP, 'Not a planet file: %s' % ex); return
        self.planet = pl; self._fill_planet(); self.status('Loaded %s' % p)

    def reset_planet(self):
        if messagebox.askyesno(APP, 'Reset every colour to Earth?'):
            self.planet = L.load_planet(); self._fill_planet()

    def export_swatch(self):
        p = filedialog.asksaveasfilename(defaultextension='.png', initialfile='planet_swatch.png', filetypes=[('PNG', '*.png')])
        if not p: return
        im, layout = L.planet_swatch(self.planet)
        im.save(p)
        with open(os.path.splitext(p)[0] + '.layout.json', 'w') as f: json.dump(layout, f)
        self.status('Swatch saved. Repaint the squares in Krita (keep the size), then Import swatch.')

    def import_swatch(self):
        p = filedialog.askopenfilename(filetypes=[('PNG', '*.png')])
        if not p: return
        lay = os.path.splitext(p)[0] + '.layout.json'
        if not os.path.exists(lay):
            messagebox.showerror(APP, 'The swatch needs its layout file next to it:\n' + lay); return
        try:
            self.planet = L.planet_from_swatch(self.planet, Image.open(p), json.load(open(lay)))
        except Exception as ex:
            messagebox.showerror(APP, 'Could not read the swatch: %s' % ex); return
        self._fill_planet(); self.status('Swatch imported. Save to keep it.')

    def preview_planet(self):
        self.save_planet(); was = self.planet_on.get()
        self.project.meta['planet_enabled'] = True
        self.nb.select(1); self.live.set(True); self.refresh_preview()
        self.project.meta['planet_enabled'] = was

    # ------------------------------------------------------------------ spec
    def _spec_tab(self):
        tab = ttk.Frame(self.nb); self.nb.add(tab, text='  Spec  ')
        bar = ttk.Frame(tab); bar.pack(fill='x', padx=8, pady=8)
        ttk.Button(bar, text='Export this spec...', command=self.export_spec).pack(side='left')
        ttk.Button(bar, text='Export creature palette...', command=self.export_palette).pack(side='left', padx=8)
        self.spec = tk.Text(tab, wrap='word', bg='#1d2024', fg='#e6e6e6', relief='flat', font=('Segoe UI', 10), padx=12, pady=10)
        self.spec.pack(fill='both', expand=True, padx=8, pady=(0, 8))

    def export_spec(self):
        if not self.creature: return
        p = filedialog.asksaveasfilename(defaultextension='.txt', initialfile=self.creature['id'] + '_spec.txt')
        if p: open(p, 'w').write(L.spec_text(self.creature, self.best)); self.status('Saved ' + p)

    def export_specs(self):
        d = filedialog.askdirectory(title='Folder for the specs')
        if not d: return
        for c in self.best['creatures']: open(os.path.join(d, c['id'] + '_spec.txt'), 'w').write(L.spec_text(c, self.best))
        self.status('%d specs written to %s' % (len(self.best['creatures']), d))

    def export_palette(self):
        d = filedialog.askdirectory(title='Folder for the palette files')
        if not d: return
        self.pal.write_gpl(os.path.join(d, 'mantisplanet_creatures.gpl')); self.pal.write_aco(os.path.join(d, 'mantisplanet_creatures.aco'))
        self.pal.swatch_png(os.path.join(d, 'mantisplanet_creatures.png'))
        self.status('Palette written: .gpl (Krita, GIMP), .aco (Photoshop), .png')

    # ------------------------------------------------------------------ project, card, settings
    def open_project(self):
        d = filedialog.askdirectory(title='Project folder', initialdir=self.project.folder)
        if not d: return
        self.project = L.Project(d); self.settings['project'] = d; save_settings(self.settings)
        self.planet = L.load_planet(self.project.planet_path()) if os.path.exists(self.project.planet_path()) else L.load_planet()
        self.planet_on.set(bool(self.project.meta.get('planet_enabled'))); self._fill_planet()
        self.on_creature(); self.status('Project: ' + d)

    def export_pack(self):
        card = self.settings.get('card') or filedialog.askdirectory(title='The SD card (its top folder)')
        if not card: return
        if not os.path.isdir(card):
            messagebox.showerror(APP, 'The card folder is not there:\n' + card); return
        if not messagebox.askyesno(APP, 'Write the pack to %s ?' % os.path.join(card, 'mp-sprites')): return
        try:
            n = self.project.export(card)
        except Exception as ex:
            messagebox.showerror(APP, 'Export failed: %s' % ex); return
        self.settings['card'] = card; save_settings(self.settings)
        self.status('%d files written to %s. Put the card in the Core2 and restart it.' % (n, os.path.join(card, 'mp-sprites')))
        self.msg('Exported %d files to %s' % (n, card))

    def settings_dialog(self):
        w = tk.Toplevel(self); w.title('Settings'); w.configure(bg='#2c3036'); w.transient(self); w.grab_set()
        vals = {k: tk.StringVar(value=self.settings.get(k, '')) for k in ('project', 'card', 'mpview')}
        rows = (('project', 'Project folder', True), ('card', 'SD card folder', True), ('mpview', 'Live renderer (mpview.exe)', False))
        for r, (k, label, isdir) in enumerate(rows):
            ttk.Label(w, text=label).grid(row=r, column=0, sticky='w', padx=10, pady=6)
            ttk.Entry(w, textvariable=vals[k], width=58).grid(row=r, column=1, padx=6)
            ttk.Button(w, text='...', width=3, command=lambda k=k, d=isdir: vals[k].set((filedialog.askdirectory() if d else filedialog.askopenfilename()) or vals[k].get())).grid(row=r, column=2, padx=6)
        def ok():
            for k, v in vals.items(): self.settings[k] = v.get()
            save_settings(self.settings)
            if self.settings.get('project') and os.path.abspath(self.settings['project']) != os.path.abspath(self.project.folder):
                self.project = L.Project(self.settings['project']); self.on_creature()
            w.destroy()
        ttk.Button(w, text='OK', style='Accent.TButton', command=ok).grid(row=4, column=1, sticky='e', pady=10)

    def live_help(self):
        messagebox.showinfo(APP, 'The live renderer is the game itself, built for Windows.\n\n'
                            '1. Install MSYS2 (msys2.org). Open "MSYS2 MINGW64".\n'
                            '2. pacman -S --needed mingw-w64-x86_64-gcc\n'
                            '3. cd to the repository\'s tools/host folder and run:  sh build_mpview.sh\n'
                            '4. In Settings, point "Live renderer" at tools/host/mpview.exe and tick "Live renderer".')

    def _quit(self):
        save_settings(self.settings); self.destroy()

if __name__ == '__main__':
    Studio().mainloop()
