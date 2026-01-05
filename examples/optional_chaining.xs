// XS Language -- Safe Navigation (without ?. operator)
// ?. and ?? are not yet supported
// Instead, use explicit null checks

struct Address { city, zip }
struct User { name, age, address }
struct Company { name, ceo }

fn make_user(name, age, city) {
    if city != null {
        return User { name: name, age: age, address: Address { city: city, zip: "00000" } }
    } else {
        return User { name: name, age: age, address: null }
    }
}

fn main() {
    println("=== Safe Navigation Demo ===")
    println("")

    let alice = make_user("Alice", 30, "New York")
    let bob = make_user("Bob", 25, null)

    // Direct field access (safe when we know it exists)
    println("Alice's city: {alice.address.city}")
    println("")

    // Safe navigation with null checks
    println("--- Explicit null checks ---")
    if alice.address != null {
        println("Alice address city: {alice.address.city}")
        println("Alice address zip:  {alice.address.zip}")
    } else {
        println("Alice has no address")
    }

    if bob.address != null {
        println("Bob address city: {bob.address.city}")
    } else {
        println("Bob has no address (null)")
    }
    println("")

    // Safe access helper function
    fn get_city(user) {
        if user.address != null {
            return user.address.city
        }
        return null
    }

    println("--- Safe access function ---")
    let alice_city = get_city(alice)
    let bob_city = get_city(bob)
    println("Alice city: {alice_city}")
    println("Bob city:   {bob_city}")
    println("")

    // Default values with null check
    fn value_or(val, default) {
        if val == null {
            return default
        }
        return val
    }

    println("--- With defaults ---")
    println("Alice city: {value_or(get_city(alice), "Unknown")}")
    println("Bob city:   {value_or(get_city(bob), "Unknown")}")
    println("")

    // Nested struct navigation
    println("--- Nested navigation ---")
    let c1 = Company { name: "Acme", ceo: make_user("Carol", 40, "Boston") }
    let c2 = Company { name: "Empty Corp", ceo: null }

    fn get_ceo_city(company) {
        if company.ceo != null {
            if company.ceo.address != null {
                return company.ceo.address.city
            }
        }
        return "no city"
    }

    println("{c1.name} CEO city: {get_ceo_city(c1)}")
    println("{c2.name} CEO city: {get_ceo_city(c2)}")
    println("")

    // Map-based safe access (maps support string key indexing)
    println("--- Map safe access ---")
    let config = #{"database": #{"host": "localhost", "port": 5432}, "cache": null}

    fn safe_map_get(m, key) {
        if m == null { return null }
        return m[key]
    }

    let db_host = safe_map_get(config.database, "host")
    let cache_host = safe_map_get(config.cache, "host")
    println("db host:    {value_or(db_host, "N/A")}")
    println("cache host: {value_or(cache_host, "N/A")}")
    println("")

    // Array null checks
    println("--- Array null checks ---")
    let arr = [10, 20, 30]
    let none = null
    println("arr length: {len(arr)}")
    if none != null {
        println("none length: {len(none)}")
    } else {
        println("none is null, no length")
    }
}
