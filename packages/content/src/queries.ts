// Run with `perspective: "published"`: drafts never reach the site.
export const PUBLISHED_QUERY = `{
  "synths": *[_type == "synth" && origin == "own"] | order(order asc),
  "releases": *[_type == "release" && synth->origin == "own"] | order(date desc)
}`;
