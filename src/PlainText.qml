import QtQuick

// Text that is never read as markup. Names that come from outside (Wi-Fi
// networks, files, processes, devices) can contain HTML, and plain Text would
// render it, even fetching images from the web. Every label in Keel uses
// this; the few that need styling set textFormat themselves and build their
// markup only from Keel's own words.
//
// Figures are tabular everywhere, so columns of numbers line up and a value
// that ticks over doesn't make its neighbours jitter.
Text {
    textFormat: Text.PlainText
    color: theme.foreground
    font.pixelSize: 13
    font.features: { "tnum": 1 }
}
