use crate::grug::GrugStore;

/// Builder for a styled HTML page with optional table listing and form.
pub struct HtmlPage {
    title: String,
    parts: Vec<String>,
}

impl HtmlPage {
    pub fn new(title: &str) -> Self {
        Self {
            title: title.to_string(),
            parts: Vec::new(),
        }
    }

    /// Add a table listing all entries from a GrugStore.
    pub fn list(&mut self, store: &GrugStore) {
        let mut h = String::new();
        h.push_str("<table><tr>");

        if let Some(first) = store.entries.first() {
            for (k, _) in &first.fields {
                h.push_str(&format!("<th>{}</th>", esc(k)));
            }
            h.push_str("<th></th></tr>");

            for entry in &store.entries {
                h.push_str("<tr>");
                for (_, v) in &entry.fields {
                    h.push_str(&format!("<td>{}</td>", esc(v)));
                }
                h.push_str(&format!(
                    "<td><a href='/delete?id={}'>delete</a></td></tr>",
                    esc(&entry.id)
                ));
            }
        } else {
            h.push_str("</tr><tr><td>No entries yet</td></tr>");
        }

        h.push_str("</table>");
        self.parts.push(h);
    }

    /// Add a form that POSTs to the given action URL with the given fields.
    pub fn form(&mut self, action: &str, fields: &[&str]) {
        let mut h = String::new();
        h.push_str(&format!(
            "<hr><h2>Add new</h2><form method='POST' action='{}'>",
            esc(action)
        ));
        for f in fields {
            h.push_str(&format!(
                "<label>{}: <input name='{}' required></label><br><br>",
                esc(f),
                esc(f)
            ));
        }
        h.push_str("<button type='submit'>Create</button></form>");
        self.parts.push(h);
    }

    /// Render the full HTML page as a string.
    pub fn render(&self) -> String {
        let mut html = String::with_capacity(8192);
        html.push_str("<!DOCTYPE html><html><head><meta charset='utf-8'>");
        html.push_str("<style>");
        html.push_str(
            "body{font-family:sans-serif;max-width:800px;margin:40px auto;padding:0 20px}",
        );
        html.push_str("table{width:100%;border-collapse:collapse}");
        html.push_str("th,td{padding:8px;border:1px solid #ddd;text-align:left}");
        html.push_str("th{background:#333;color:#fff}a{color:#c33}");
        html.push_str("</style>");
        html.push_str(&format!("<title>{}</title>", esc(&self.title)));
        html.push_str("</head><body>");
        html.push_str(&format!("<h1>{}</h1>", esc(&self.title)));

        for part in &self.parts {
            html.push_str(part);
        }

        html.push_str("</body></html>");
        html
    }
}

fn esc(s: &str) -> String {
    s.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
}
