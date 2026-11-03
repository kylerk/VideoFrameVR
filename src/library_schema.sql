-- The media library, schema version 1, built into the app. Later versions
-- add numbered migrations; this file stays the version 1 starting point.

CREATE TABLE meta (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

-- One row per video, whatever file or stream it was seen as.
CREATE TABLE videos (
  id               INTEGER PRIMARY KEY,
  oshash           TEXT UNIQUE,              -- files: 16 lowercase hex digits, as Stash stores it
  stream_key       TEXT UNIQUE,              -- links: the address (earlier: youtube:<id>)
  rating           REAL,                     -- NULL = unrated, else 0.5 .. 5.0 in steps of 0.5
  play_count       INTEGER NOT NULL DEFAULT 0,
  last_played_at   INTEGER,                  -- Unix seconds, UTC
  added_at         INTEGER,                  -- Unix seconds, UTC: first seen
  resume_seconds   REAL,
  duration_seconds REAL,
  settings         TEXT,                     -- the player's per-video settings, JSON
  CHECK (oshash IS NOT NULL OR stream_key IS NOT NULL),
  CHECK (rating IS NULL OR (rating >= 0.5 AND rating <= 5.0))
);

-- Where each video's files were last seen: a cache that can be rebuilt.
CREATE TABLE files (
  path     TEXT PRIMARY KEY,
  folder   TEXT NOT NULL,                    -- the path's parent folder
  video_id INTEGER NOT NULL REFERENCES videos(id) ON DELETE CASCADE,
  size     INTEGER NOT NULL,                 -- bytes
  mtime    INTEGER NOT NULL,                 -- Unix seconds
  seen_at  INTEGER NOT NULL                  -- Unix seconds, UTC
);

CREATE INDEX files_folder ON files(folder);
CREATE INDEX files_size ON files(size);
CREATE INDEX files_video ON files(video_id);
CREATE INDEX videos_rating ON videos(rating);
CREATE INDEX videos_play_count ON videos(play_count);
CREATE INDEX videos_last_played ON videos(last_played_at);

INSERT INTO meta (key, value) VALUES ('schema_version', '1');
