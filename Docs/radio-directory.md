# Radio directory outages

The Audio tab includes nine HTTPS MP3 streams in five countries: France,
Germany, the Netherlands, Switzerland and the United States. Bundled country
and station choices appear immediately while Radio Browser is contacted.
They do not replace device favorites or automatically start playback.

Successful directory responses replace the displayed list and are saved in
browser local storage. During an outage, a saved list less than 30 days old
has priority over bundled choices. The cache retains the countries and the
eight most recent station lists. It is scoped to each HTTP/HTTPS origin and
browser; clearing browser data removes it. The bundled list remains available.
Countries outside the bundled set require a saved or live directory response.
Retry checks the service again. Live streams still require Internet access.

The firmware serves these assets precompressed with its normal web bundle;
station discovery, JSON processing and caching run in the browser. Directory
and bundled stream addresses use HTTPS. Device playback uses the existing
stream decoder and does not relay music through the browser.

Stream sources, checked 2026-10-11 with bounded GET requests: all nine returned
HTTP 200, `audio/mpeg` and at least 2048 bytes of streaming data. This confirms
endpoint availability, rather than audible quality on every receiving device.

- [Radio France Open API](https://developers.radiofrance.fr/doc/tutorial-by-example/list-locals-and-webradios)
- [Deutschlandfunk stream links](https://www.deutschlandfunk.de/livestream-100.html)
- [Radio Swiss Jazz](https://www.radioswissjazz.ch/de/empfang/internet)
- [Radio Swiss Classic](https://www.radioswissclassic.ch/de/empfang/internet)
- [SomaFM Groove Salad](https://somafm.com/groovesalad/directstreamlinks.html)
- [SomaFM Drone Zone](https://somafm.com/dronezone/directstreamlinks.html)
- [NPO Klassiek](https://www.npoklassiek.nl/streams)

Stream addresses can change; firmware updates can refresh bundled entries.
Successful Radio Browser responses keep browser caches current independently.

## SD folders

After each successful SPI or SDMMC mount, the firmware checks `/web`,
`/record`, `/security`, `/3d`, `/media` and `/media/wav` and creates missing directories. This runs
at boot, after card insertion, after confirmed formatting and after a manual
eject/mount cycle. Checking occurs under the existing storage maintenance
lease, before readers or recorders can use the mounted card. Existing files
are preserved. A regular file blocking a required folder, card read error or
failed directory creation is reported in the device log. There is no automatic
formatting, asset compression or creation of placeholder web entry files.
Audio playback can select files from other folders; Audio Effects uses `/media/wav`.
The generated root directory index is invalidated on mount so newly inserted
files and automatic folders appear in the file manager.

## Azerbaijan and Russia fallback

Azerbaijan defaults to AvtoFM, using the same working stream saved on the device:
`http://5.191.241.101:8000/avtofm`. It is fetched by the firmware, not the browser,
so an HTTPS control page does not load insecure mixed content. Playback is never
started automatically by selecting the default. Existing user selections are retained.
Russia includes Record and Record Russian Mix. The 64 kbps
AAC streams are published by [Radio Record's station API](https://www.radiorecord.ru/api/stations/).
Offline cache results include newly shipped fallback countries and stations; live
Radio Browser results still refresh the cache.
