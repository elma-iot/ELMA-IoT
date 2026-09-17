import {installCountrySearch} from './country-search.js';
export function createRadioBrowserModule({
  state,
  elements,
  radioSelectionStorageKey,
  defaultRadioSelection,
  updatePlaybackHeroControls,
  isPlaybackActive,
  submitPlay,
}) {
  function loadSavedRadioSelection() {
    try {
      const stored = JSON.parse(window.localStorage.getItem(radioSelectionStorageKey) || "{}");
      return {
        country: String(stored.country || "").trim(),
        stationName: String(stored.stationName || "").trim(),
        stationUrl: String(stored.stationUrl || "").trim(),
      };
    } catch {
      return { country: "", stationName: "", stationUrl: "" };
    }
  }

  function preferredRadioSelection() {
    const savedSelection = loadSavedRadioSelection();
    return {
      country: savedSelection.country || defaultRadioSelection.country,
      stationName: savedSelection.stationName || defaultRadioSelection.stationName,
      stationUrl: savedSelection.stationUrl || defaultRadioSelection.stationUrl,
    };
  }

  function saveRadioSelection(selection) {
    try {
      window.localStorage.setItem(radioSelectionStorageKey, JSON.stringify({
        country: String(selection?.country || "").trim(),
        stationName: String(selection?.stationName || "").trim(),
        stationUrl: String(selection?.stationUrl || "").trim(),
      }));
    } catch {
    }
  }

  function radioBrowserApiUrl(path) {
    return `https://all.api.radio-browser.info/json${path}`;
  }

  function setRadioBrowserStatus(message, isError = false) {
    if (!elements.radioBrowserStatus) {
      return;
    }
    elements.radioBrowserStatus.textContent = message;
    elements.radioBrowserStatus.style.color = isError ? "#b42318" : "";
  }

  function resetRadioStationSelect(placeholder = "Select a country first") {
    if (!elements.radioStationSelect) {
      return;
    }
    elements.radioStationSelect.innerHTML = "";
    const option = document.createElement("option");
    option.value = "";
    option.textContent = placeholder;
    elements.radioStationSelect.appendChild(option);
    elements.radioStationSelect.value = "";
    elements.radioStationSelect.disabled = true;
    updatePlaybackHeroControls();
  }

  function renderRadioCountries(countries) {
    if (!elements.radioCountrySelect) {
      return;
    }

    installCountrySearch(elements.radioCountrySelect);
    const savedSelection = preferredRadioSelection();
    const previousValue = elements.radioCountrySelect.value || savedSelection.country;
    elements.radioCountrySelect.innerHTML = "";

    const placeholder = document.createElement("option");
    placeholder.value = "";
    placeholder.textContent = countries.length ? "Select a country" : "No countries available";
    elements.radioCountrySelect.appendChild(placeholder);

    countries.forEach((country) => {
      const option = document.createElement("option");
      option.value = country.name;
      option.dataset.countryCode=country.code||"";
      option.dataset.stationCount=String(country.stationCount);
      let label=country.name;
      try { if(country.code) label=new Intl.DisplayNames([document.documentElement.lang||"en"],{type:"region"}).of(country.code)||label; } catch {}
      option.textContent = `${label} (${country.stationCount})`;
      elements.radioCountrySelect.appendChild(option);
    });

    if (countries.some((country) => country.name === previousValue)) {
      elements.radioCountrySelect.value = previousValue;
    }
  }

  function renderRadioStations(stations) {
    if (!elements.radioStationSelect) {
      return;
    }

    elements.radioStationSelect.innerHTML = "";

    const placeholder = document.createElement("option");
    placeholder.value = "";
    placeholder.textContent = stations.length ? "Select a station" : "No stations found";
    elements.radioStationSelect.appendChild(placeholder);

    stations.forEach((station, index) => {
      const option = document.createElement("option");
      option.value = String(index);
      option.textContent = station.name;
      elements.radioStationSelect.appendChild(option);
    });

    elements.radioStationSelect.disabled = !stations.length;
    updatePlaybackHeroControls();
  }

  async function loadRadioCountries(forceRefresh = false) {
    if (state.radioCountriesLoading) {
      return;
    }
    if (state.radioCountries.length && !forceRefresh) {
      renderRadioCountries(state.radioCountries);
      return;
    }

    state.radioCountriesLoading = true;
    setRadioBrowserStatus("Loading countries...");

    try {
      const response = await fetch(radioBrowserApiUrl("/countries"), {
        headers: { Accept: "application/json" },
        cache: "no-store",
      });
      if (!response.ok) {
        throw new Error(`Radio Browser countries failed: ${response.status}`);
      }

      const payload = await response.json();
      state.radioCountries = (Array.isArray(payload) ? payload : [])
        .map((country) => ({
          name: String(country.name || "").trim(),
          stationCount: Number(country.stationcount || 0),
          code: String(country.iso_3166_1 || country.iso3166_1 || "").toUpperCase(),
        }))
        .filter((country) => country.name)
        .sort((left, right) => left.name.localeCompare(right.name, undefined, { sensitivity: "base" }));

      renderRadioCountries(state.radioCountries);
      resetRadioStationSelect();
      setRadioBrowserStatus(state.radioCountries.length ? "Choose a country to load stations." : "No countries available.");

      const savedSelection = preferredRadioSelection();
      if (savedSelection.country && state.radioCountries.some((country) => country.name === savedSelection.country)) {
        elements.radioCountrySelect.value = savedSelection.country;
        await loadRadioStations(savedSelection.country);
      }
    } catch (error) {
      renderRadioCountries([]);
      resetRadioStationSelect("Radio Browser unavailable");
      setRadioBrowserStatus(error.message, true);
    } finally {
      state.radioCountriesLoading = false;
    }
  }

  async function loadRadioStations(countryName) {
    const trimmedCountry = String(countryName || "").trim();
    state.radioStations = [];

    if (!trimmedCountry) {
      resetRadioStationSelect();
      setRadioBrowserStatus(state.radioCountries.length ? "Choose a country to load stations." : "Loading countries...");
      return;
    }

    state.radioStationsLoading = true;
    resetRadioStationSelect("Loading stations...");
    setRadioBrowserStatus(`Loading stations for ${trimmedCountry}...`);
    saveRadioSelection({ country: trimmedCountry });

    try {
      const response = await fetch(
        `${radioBrowserApiUrl(`/stations/bycountry/${encodeURIComponent(trimmedCountry)}`)}?hidebroken=true&order=name`,
        {
          headers: { Accept: "application/json" },
          cache: "no-store",
        },
      );
      if (!response.ok) {
        throw new Error(`Radio Browser stations failed: ${response.status}`);
      }

      const payload = await response.json();
      state.radioStations = (Array.isArray(payload) ? payload : [])
        .map((station) => ({
          name: String(station.name || "").trim(),
          url: String(station.url_resolved || station.url || "").trim(),
          codec: String(station.codec || "").trim(),
          bitrate: Number(station.bitrate || 0),
        }))
        .filter((station) => station.name && station.url)
        .sort((left, right) => left.name.localeCompare(right.name, undefined, { sensitivity: "base" }));

      renderRadioStations(state.radioStations);

      const savedSelection = preferredRadioSelection();
      const savedIndex = state.radioStations.findIndex((station) => (
        (savedSelection.stationUrl && station.url === savedSelection.stationUrl) ||
        (savedSelection.stationName && station.name === savedSelection.stationName)
      ));
      if (savedIndex >= 0 && elements.radioStationSelect) {
        elements.radioStationSelect.value = String(savedIndex);
        applySelectedRadioStation();
      }

      setRadioBrowserStatus(
        state.radioStations.length
          ? `Loaded ${state.radioStations.length} station(s) for ${trimmedCountry}.`
          : `No stations found for ${trimmedCountry}.`,
      );
    } catch (error) {
      resetRadioStationSelect("Station list unavailable");
      setRadioBrowserStatus(error.message, true);
    } finally {
      state.radioStationsLoading = false;
    }
  }

  async function applySelectedRadioStation(options = {}) {
    const { autoPlay = false } = options;
    const selectedIndex = Number(elements.radioStationSelect?.value ?? -1);
    if (!Number.isInteger(selectedIndex) || selectedIndex < 0 || selectedIndex >= state.radioStations.length) {
      return;
    }

    const station = state.radioStations[selectedIndex];
    if (!station) {
      return;
    }

    if (elements.playUrl) {
      elements.playUrl.value = station.url;
    }
    if (elements.playLabel) {
      elements.playLabel.value = station.name;
    }
    if (elements.playType) {
      elements.playType.value = "stream";
    }

    saveRadioSelection({
      country: elements.radioCountrySelect?.value || "",
      stationName: station.name,
      stationUrl: station.url,
    });

    const meta = [];
    if (station.codec) {
      meta.push(station.codec.toUpperCase());
    }
    if (station.bitrate > 0) {
      meta.push(`${station.bitrate} kbps`);
    }
    setRadioBrowserStatus(meta.length ? `${station.name} selected (${meta.join(" | ")}).` : `${station.name} selected.`);

    if (!autoPlay || !isPlaybackActive() || state.playbackActionInProgress) {
      return;
    }

    if (!elements.playForm?.reportValidity()) {
      return;
    }

    setRadioBrowserStatus(`Switching to ${station.name}...`);
    await submitPlay();
  }

  return {
    loadRadioCountries,
    loadRadioStations,
    applySelectedRadioStation,
  };
}