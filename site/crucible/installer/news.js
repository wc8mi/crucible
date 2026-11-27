// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Behnke
// The front page's news (tools/crucible_site.py writes data/news.json, newest first): the page holds the first posts,
// More adds the next ones from the feed. The decisions without the DOM, so the tests can run them.

// The next posts after the `shown` ones on the page: {posts, done}; done once nothing is left after them
export function nextPosts(feed, shown, n = feed?.first || 5) {
	const all = Array.isArray(feed?.posts) ? feed.posts : [];
	const posts = all.slice(shown, shown + n);
	return { posts, done: shown + posts.length >= all.length };
}

// How many More presses bring the post `id` onto the page (0: already there or not in the feed)
export function pressesFor(feed, shown, id, n = feed?.first || 5) {
	const at = (feed?.posts || []).findIndex((p) => p.id === id);
	return at < shown ? 0 : Math.ceil((at + 1 - shown) / n);
}
