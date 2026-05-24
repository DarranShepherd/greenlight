// Greenlight landing page — flasher + waitlist wiring.
//
// The firmware manifest is generated in-browser per selected board so we
// can ship new variants without rewriting the page. Manifests live at
// `firmware/metadata.json`, produced by the release workflow.

const KIT_FORM_ENDPOINT = 'https://app.kit.com/forms/9480016/subscriptions';

function track(event, data) {
	if (typeof window === 'undefined' || !window.umami) return;
	try {
		window.umami.track(event, data);
	} catch {
		/* analytics never blocks UX */
	}
}

const BOARDS = {
	esp32_2432s028_ili9341: {
		label: 'ESP32-2432S028 (Guition, ILI9341) — 2.8"',
		chipFamily: 'ESP32',
		firmware: 'firmware/firmware-esp32_2432s028_ili9341-merged.bin'
	},
	esp32_2432s028_st7789: {
		label: 'ESP32-2432S028 (Unbranded, ST7789) — 2.8"',
		chipFamily: 'ESP32',
		firmware: 'firmware/firmware-esp32_2432s028_st7789-merged.bin'
	},
	esp32_32e_st7789: {
		label: 'ESP32-32E (ST7789) — 3.2"',
		chipFamily: 'ESP32',
		firmware: 'firmware/firmware-esp32_32e_st7789-merged.bin'
	}
};

let releaseMetadata = null;
let currentManifestUrl = null;

async function loadReleaseMetadata() {
	try {
		const response = await fetch('firmware/metadata.json', { cache: 'no-cache' });
		if (!response.ok) return null;
		return await response.json();
	} catch {
		return null;
	}
}

function buildManifest(boardId) {
	const board = BOARDS[boardId];
	if (!board) return null;

	const version = releaseMetadata?.version ?? 'unreleased';
	const firmwarePath = new URL(board.firmware, window.location.href).toString();

	return {
		name: 'Greenlight',
		version,
		new_install_prompt_erase: false,
		builds: [
			{
				chipFamily: board.chipFamily,
				parts: [{ path: firmwarePath, offset: 0 }]
			}
		]
	};
}

function manifestUrlFor(boardId) {
	const manifest = buildManifest(boardId);
	if (!manifest) return null;
	const blob = new Blob([JSON.stringify(manifest)], { type: 'application/json' });
	return URL.createObjectURL(blob);
}

function browserSupportsWebSerial() {
	return 'serial' in navigator;
}

function renderInstallButton(boardId) {
	const target = document.getElementById('flash-target');
	if (!target) return;
	target.innerHTML = '';

	if (!boardId) {
		const placeholder = document.createElement('button');
		placeholder.type = 'button';
		placeholder.disabled = true;
		placeholder.className =
			'cursor-not-allowed rounded-lg bg-surface-container-high px-8 py-4 font-bold text-on-surface-variant';
		placeholder.textContent = 'Install Greenlight';
		target.appendChild(placeholder);
		return;
	}

	if (currentManifestUrl) URL.revokeObjectURL(currentManifestUrl);
	currentManifestUrl = manifestUrlFor(boardId);

	const installButton = document.createElement('esp-web-install-button');
	installButton.setAttribute('manifest', currentManifestUrl);

	const slotButton = document.createElement('button');
	slotButton.slot = 'activate';
	slotButton.type = 'button';
	slotButton.className =
		'btn-primary text-lg';
	slotButton.textContent = 'Install Greenlight';
	slotButton.addEventListener('click', () => track('flash-clicked', { board: boardId }));
	installButton.appendChild(slotButton);

	installButton.addEventListener('state-changed', (event) => {
		const state = event.detail?.state;
		if (state === 'finished') track('flash-completed', { board: boardId });
		else if (state === 'error') track('flash-failed', { board: boardId });
	});

	const unsupported = document.createElement('span');
	unsupported.slot = 'unsupported';
	unsupported.className = 'text-sm text-on-surface-variant';
	unsupported.textContent =
		'Your browser does not support Web Serial. Open this page in Chrome or Edge on a desktop computer.';
	installButton.appendChild(unsupported);

	target.appendChild(installButton);
}

function renderVersionInfo(boardId) {
	const el = document.getElementById('flash-version');
	if (!el) return;

	if (!releaseMetadata) {
		el.textContent = 'No firmware release has been published yet.';
		return;
	}

	if (!boardId) {
		el.textContent = `Latest release: v${releaseMetadata.version}`;
		return;
	}

	const variant = releaseMetadata.variants?.[boardId];
	if (!variant) {
		el.textContent = `Latest release (v${releaseMetadata.version}) does not include this board.`;
		return;
	}
	el.textContent = `Installing v${releaseMetadata.version} for ${BOARDS[boardId].label}.`;
}

function wireDeviceSelector() {
	const select = document.getElementById('device-select');
	if (!select) return;

	select.addEventListener('change', () => {
		const boardId = select.value || null;
		renderInstallButton(boardId);
		renderVersionInfo(boardId);
	});

	renderInstallButton(null);
	renderVersionInfo(null);
}

function showBrowserWarningIfNeeded() {
	if (browserSupportsWebSerial()) return;
	const warning = document.getElementById('flash-browser-warning');
	if (warning) warning.classList.remove('hidden');
}

function setWaitlistStatus(message, kind) {
	const status = document.getElementById('waitlist-status');
	if (!status) return;
	status.textContent = message;
	status.classList.remove('hidden');
	status.classList.remove('text-error', 'text-on-primary-container');
	if (kind === 'error') {
		status.classList.add('text-error');
	} else {
		status.classList.add('text-on-primary-container');
	}
}

function wireWaitlistForm() {
	const form = document.getElementById('waitlist-form');
	if (!form) return;

	form.addEventListener('submit', async (event) => {
		event.preventDefault();

		const emailInput = document.getElementById('waitlist-email');
		const email = emailInput?.value.trim();
		if (!email) {
			setWaitlistStatus('Please enter your email address.', 'error');
			return;
		}

		const submitButton = form.querySelector('button[type="submit"]');
		if (submitButton) {
			submitButton.disabled = true;
			submitButton.textContent = 'Joining…';
		}

		try {
			const response = await fetch(KIT_FORM_ENDPOINT, {
				method: 'POST',
				headers: {
					Accept: 'application/json',
					'Content-Type': 'application/x-www-form-urlencoded'
				},
				body: new URLSearchParams({ email_address: email })
			});

			if (!response.ok) throw new Error(`Kit responded with ${response.status}`);

			form.reset();
			setWaitlistStatus(
				'Success! Now check your email to confirm your subscription.',
				'success'
			);
			track('waitlist-joined');
		} catch (error) {
			console.error('Waitlist submission failed', error);
			setWaitlistStatus(
				"Something went wrong. Please try again, or email greenlight@ampernomics.com.",
				'error'
			);
		} finally {
			if (submitButton) {
				submitButton.disabled = false;
				submitButton.textContent = 'Join waitlist';
			}
		}
	});
}

async function init() {
	releaseMetadata = await loadReleaseMetadata();
	wireDeviceSelector();
	showBrowserWarningIfNeeded();
	wireWaitlistForm();
}

init();
